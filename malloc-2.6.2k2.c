/* 
  A version of malloc/free/realloc written by Doug Lea and released to the 
  public domain.  Send questions/comments/complaints/performance data
  to dl@cs.oswego.edu

* preliminary VERSION 2.6.2k Sun Dec 24 12:08:41 1995  Doug Lea  (dl at gee)
  
   Note: There may be an updated version of this malloc obtainable at
           ftp://g.oswego.edu/pub/misc/malloc.c
         Check before installing!

* Overview

  Vital statistics:

   Alignment:                            8-byte
   Assumed pointer representation:       4 bytes
   Assumed size_t  representation:       4 bytes
   Minimum wastage per allocated chunk:  4 bytes
   Maximum wastage per allocated chunk: 24 bytes 
   Minimum allocated size:              16 bytes (12 bytes usable, 4 overhead)
   Maximum allocated size:      2147483640 (2^31 - 8) bytes

   Explanations:

       Malloced chunks have space overhead of 4 bytes for the size
       field.  When a chunk is in use, only the `front' size is used,
       plus a bit in the NEXT adjacent chunk saying that its previous
       chunk is in use.

       When a chunk is freed, 12 additional bytes are needed; 4 for
       the trailing size field and 8 bytes for free list
       pointers. Thus, the minimum allocatable size is 16 bytes,
       of which 12 bytes are usable.

       It is assumed that 32 bits suffice to represent chunk sizes.
       The maximum size chunk is 2^31 - 8 bytes.  

       malloc(0) returns a pointer to something of the minimum
       allocatable size.  Requests for negative sizes (when size_t is
       signed) or those greater than (2^31 - 8) bytes will also return
       a minimum-sized chunk. 

       8 byte alignment is currently hardwired into the design.  This
       seems to suffice for all current machines and C compilers.
       Calling memalign will return a chunk that is both 8-byte
       aligned and meets the requested (power of two) alignment.

       Alignnment demands, plus the minimum allocatable size restriction
       make the worst-case wastage 24 bytes. This occurs only for
       a request of zero. The worst case for requests >= 16 bytes is 15
       bytes. (Empirically, average wastage is around 5 to 7 bytes.)


  Structure:

    This malloc, like any other, is a compromised design. 

    Chunks of memory are maintained using a `boundary tag' method as
    described in e.g., Knuth or Standish.  (See the paper by Paul
    Wilson ftp://ftp.cs.utexas.edu/pub/garbage/allocsrv.ps for a
    survey of such techniques.)  Sizes of free chunks are stored both
    in the front of each chunk and at the end.  This makes
    consolidating fragmented chunks into bigger chunks very fast.  The
    size fields also hold bits representing whether chunks are free or
    in use.


    Available chunks are kept in any of four places:

    * `av': An array of chunks serving as bin headers for consolidated
       chunks. Each bin is doubly linked.  The bins are approximately
       proportionally (log) spaced.  There are a lot of these bins
       (128). This may look excessive, but works very well in
       practice.  All procedures maintain the invariant that no
       consolidated chunk physically borders another one. Chunks in
       bins are kept in size order, with ties going to the
       approximately least recently used chunk.

    * `top': The top-most available chunk (i.e., the one bordering the
       end of available memory) is treated specially. It is never
       included in any bin, is always kept fully consolidated, is used
       only if no other chunk is available, and is released back to
       the system if it is very large (see TRIM_THRESHOLD).

    * `last_remainder': A bin holding only the remainder of the
       most recently split (non-top) chunk. This bin is checked
       before other non-fitting chunks, so as to provide better
       locality for runs of sequentially allocated chunks. 

    * `recycle_list': A list of chunks all of sizes less than
       the max_recycle_size) that have been returned via free
       but not yet otherwise processed.

    See below for further descriptions of these structures.

    The main allocation algorithm contains aspects of some of the most
    well-known memory allocation strategies:
      * Best fit -- when using exact matches or scanning for smallest
           usable chunks.
      * (Roving) First fit -- when using the last remainder from a 
           previous request
      * Address-ordered fit -- by keeping bins in approximately LRU 
          order, those with lower addresses tend to be used before
          other equal-sized chunks. Also, by using top-most memory only
          when necessary.
      * Quick-lists -- Normal processing is bypassed for small
          chunks that have been freed and again soon thereafter re-malloced.


    Empirically none of these strategies alone appears as good (in
    space, time, or usually both) as a mixed strategy.

* Descriptions of public routines

  malloc:

    The requested size is first converted into a usable form, `nb'.
    This currently means to add 4 bytes overhead plus possibly more to
    obtain 8-byte alignment and/or to obtain a size of at least
    MINSIZE (currently 16 bytes), the smallest allocatable size.
    (All fits are considered `exact' if they are within MINSIZE bytes.)

    From there, the first successful of the following steps is
    taken. A few steps differ slightly for `small' (< 504 bytes)
    versus other requests:

      1. If the most recently returned (via free) chunk is of exactly
         the right size and borders another in-use chunk it is taken.

      2. For small requests, the bin corresponding to the request size
         is scanned, and if a chunk of exactly the right size is found,
         it is taken.

      3. The rest of the recycle_list is processed: If a chunk exactly
         fitting is found, it is taken, otherwise the chunk is freed and
         consolidated.

      4. If a non-small request, the bin corresponding to the request
         size is scanned, as in step (2). (The only reason these steps
         are inverted for large and small requests is that for large
         ones, consolidated recycled chunks could have generated a
         chunk that was not an exact match but was of a size that
         later turned out to be best-fitting.)

      5. The most recently remaindered chunk is used if it is
         big enough and any of the following hold:
           * It is exactly the right size
           * The remainder was created from a previous malloc call
             with a request of the same size as the current request size.
           * The request size is < 512 bytes (In other words, for this
             step, consecutive small requests are treated as if they 
             were all of the same size.)

      6. Other bins are scanned in increasing size order, using a
         chunk big enough to fulfill the request, and splitting off any
         remainder.

      7. The chunk bordering the end of memory (`top') is split off.
         If the current top is not big enough, it is extended by
         obtaining more space from the system (normally using sbrk,
         but definable to anything else via the MORECORE macro).
         Memory is gathered from the system (in system page-sized
         units) in a way that allows chunks obtained across different
         sbrk calls to be consolidated, but does not require
         contiguous memory. Thus, it should be safe to intersperse
         mallocs with other sbrk calls.

  free: 

    There are four cases:

       1. free(0) has no effect.  

       2. If the size of the chunk is <= max_recycle_size, it
          is placed on the recycle_list for later processing.

       3. If a returned chunk borders the current high end of memory,
          it is consolidated into the top, and if the total unused
          topmost memory exceeds the trim threshold, malloc_trim is
          called. The default value of the trim threshold is high enough
          so that trimming should only occur if the program is
          maintaining enough unused memory to be worth releasing.

       4. Other chunks are consolidated as they arrive, and
          placed in corresponding bins. (This includes the case of
          consolidating with the current `last_remainder').

  realloc:

    Reallocation proceeds in the usual way. If a chunk can be extended,
    it is, else a malloc-copy-free sequence is taken. 

    The old unix realloc convention of allowing the last-free'd chunk
    to be used as an argument to realloc is no longer supported.
    I don't know of any programs still relying on this feature,
    and allowing it would also allow too many other incorrect 
    usages of realloc to be sensible.

    Unless the #define REALLOC_ZERO_BYTES_FREES below is set,
    realloc with a size argument of zero (re)allocates a minimum-sized
    chunk. 

  memalign:

    memalign requests more than enough space from malloc, finds a spot
    within that chunk that meets the alignment request, and then
    possibly frees the leading and trailing space. Overreliance on
    memalign is a sure way to fragment space.

  valloc:

    valloc just invokes memalign with alignment argument equal
    to the page size of the system (or as near to this as can
    be figured out from all the includes/defines below.)

  calloc:

    calloc calls malloc, then zeroes out the allocated chunk.

  cfree:

    cfree just calls free.

  malloc_trim:

    This routine gives memory back to the system (via negative
    arguments to sbrk) if there is unused memory at the `high' end of
    the malloc pool. You can call this after freeing large blocks of
    memory to potentially reduce the system-level memory requirements
    of a program. However, it cannot guarantee to reduce memory. Under
    some allocation patterns, some large free blocks of memory will be
    locked between two used chunks, so they cannot be given back to
    the system.

    The `pad' argument to malloc_trim represents the amount of free
    trailing space to leave untrimmed. If this argument is zero,
    only the minimum amount of memory to maintain internal data
    structures will be left (one page or less). Non-zero arguments
    can be supplied to maintain enough trailing space to service
    future expected allocations without having to re-obtain memory
    from the system.

  malloc_usable_size:

    This routine tells you how many bytes you can actually use in
    an allocated chunk, which may be up to 24 bytes more than you
    requested (although typically much less; often 0). You can use
    this many bytes without worrying about overwriting other allocated
    objects. Not a particularly great programming practice, but still
    sometimes useful.

  malloc_stats:

    Prints on stderr the amount of space obtain from the system, the
    maximum amount (which may be more than current if malloc_trim got
    called), and the current number of bytes allocated via malloc (or
    realloc, etc) but not yet freed. (Note that this is the number of
    bytes allocated, not the number requested. It will be larger than
    the number requested because of overhead.)

  mallinfo:

    This version of malloc supports to the extent possible the
    standard SVID/XPG mallinfo routine that returns a struct
    containing the same kind of information you can get from
    malloc_stats. It is included mainly for use on SVID/XPG compliant
    systems that have a /usr/include/malloc.h defining struct
    mallinfo. (If you'd like to install such a thing yourself, cut out
    the preliminary declarations as described below and save them in a
    malloc.h file. But there's no compelling reason to bother to do
    this.)

    mallinfo() returns (by-copy) a mallinfo struct.  The SVID/XPG
    malloinfo struct contains a bunch of fields, most of which are not
    even meaningful in this version of malloc. They are left blank
    (zero). (Actually, I don't even know what some of them mean. These
    fields are filled with numbers that might possibly be of interest.)
    The fields that are meaningful are:

    int arena;    -- total space allocated from system 
    int ordblks;  -- number of non-inuse, non-recycling chunks 
    int smblks;   -- number of chunks in recycle list 
    int fsmblks;  -- total space in recycle list 
    int uordblks; -- total allocated space 
    int fordblks; -- total non-inuse, non-recycling space 
    int keepcost; -- top-most, releasable (via malloc_trim) space 

  mallopt:

    mallopt is the general SVID/XPG interface to tunable parameters.
    The format is to provide a (parameter-number, parameter-value) pair.
    mallopt then sets the corresponding parameter to the argument
    value if it can (i.e., so long as the value is meaningful),
    and returns 1 if successful else 0.

    To be compliant, several parameter numbers are predefined
    that have no effect on this malloc. However the following
    are supported:

    M_MXFAST (parameter number 1) is the maximum size of chunks that
      may be placed on the recycle_list when they are freed. This is a
      form of quick-list. However, unlike most implmentations of
      quick-lists, space for such small chunks is NOT segregated. If
      the space is needed for chunks of other sizes, it will be used.
  
      For small chunk sizes, the time savings from bypassing normal
      malloc processing can be significant (although hardly ever
      excessively so; even for programs that constantly allocate and
      free chunks all of the same size the observed savings is almost
      always less than 10%).

      But bypassing normal malloc processing usually also increases
      fragmentation, and thus increases space usage.  However, for
      small enough chunk sizes, the observed additional space usage is
      normally so small not to matter.
      
      Using the M_MXFAST option allows you to decide whether and how
      you'd like to make this trade-off.

      The default value is 72 bytes. This was arrived at entirely
      empirically by finding the best compromise value across a suite
      of test programs.

      Setting it to zero disables recycling all together.  Setting it
      to a value of greater than about 500 bytes is unlikely to be very
      effective, for two reasons: (1) The malloc implementation is
      tuned for the assumption that the value is small. (2)
      Empirically, it is most often fastest not to bypass normal
      processing for larger chunk sizes.

      A byproduct of setting M_MXFAST is that malloc_trim is NOT
      called from free when chunks less than max_recycle_size are
      freed. So if you want automatic trimming in programs that only
      allocate small chunks, you need to set M_MXFAST to zero. In programs
      that allocate mixtures of sizes, this generally won't matter --
      trim will get called soon enough anyway.


    M_TRIM_THRESHOLD (parameter number -1) is the maximum amount of
      unused top-most memory to keep before releasing via malloc_trim
      in free().

      Automatic trimming is mainly useful in long-lived programs.
      Because trimming can be slow, and can sometimes be wasteful (in
      cases where programs immediately afterward allocate more large
      chunks) the value should be high enough so that your overall
      system performance would improve by releasing.  As a rough
      guide, you might set to a value close to the average size of a
      process (program) running on your system.  Releasing this much
      memory would allow such a process to run in memory.

      The default value of 256K bytes appears to be a good
      compromise.  Must be greater than page size to have any useful
      effect.  To disable trimming completely, you can set to 
      (unsigned long)(-1);

    M_TOP_PAD (parameter number -2) is the amount of extra `padding' 
      space to allocate or retain whenever sbrk is called.
      It is used in two ways internally:

      * When sbrk is called to extend the top of the arena to satisfy
        a new malloc request, this much padding is added to the sbrk
        request.

      * When malloc_trim is called automatically from free(),
        it is used as the `pad' argument.

      In both cases, the actual amount of padding is rounded 
      so that the end of the arena is always a system page boundary.

      Default value is 2K bytes.

      The main reason for using padding is to avoid calling sbrk so
      often. Having even a small pad greatly reduces the likelihood
      that nearly every malloc request during program start-up (or
      after trimming) will invoke sbrk, which needlessly wastes
      time. In systems where sbrk is relatively slow, it can pay to
      increase this value, at the expense of carrying around more
      top-most memory than the program needs. Setting it to 0 reduces
      best-case (but not necessarily typical-case) memory usage to 
      a dead minimum. 


* Debugging:

    Because freed chunks may be overwritten with link fields, this
    malloc will often die when freed memory is overwritten by user
    programs.  This can be very effective (albeit in an annoying way)
    in helping users track down dangling pointers.

    If you compile with -DDEBUG, a number of assertion checks are
    enabled that will catch more memory errors. You probably won't be
    able to make much sense of the actual assertion errors, but they
    should help you locate incorrectly overwritten memory.  The
    checking is fairly extensive, and will slow down execution
    noticeably. Calling malloc_stats or mallinfo with DEBUG set will
    attempt to check every allocated and free chunk in the course of
    computing the summmaries. 

    Setting DEBUG may also be helpful if you are trying to modify 
    this code. The assertions in the check routines spell out in more 
    detail the assumptions and invariants underlying the algorithms.

* Performance differences from previous versions

    Users of malloc-2.5.X will find that generally, the current
    version conserves space better, especially when large chunks are
    allocated amid many other small ones. For example, it wastes much
    less memory when user programs occasionally do things like
    allocate space for GIF images amid other requests.  Because of the
    additional processing that leads to better behavior, it is
    just-barely detectably slower than version 2.5.3 for some (but not
    all) programs that only allocate small uniform chunks.  

    Using the default mallopt settings, Version 2.6.2 has very
    similar space characteristics as 2.6.1, but is normally
    faster. (In test cases, observed space differences range from
    about -5% to +5%, and speed improvements range from about -5% to
    +15%.  The space differences result in part from new page
    alignment policies.)


* Concurrency

    Except when compiled using the special defines below for Linux
    libc using weak aliases, this malloc is NOT designed to work in
    multithreaded applications.  No semaphores or other concurrency
    control are provided to ensure that multiple malloc or free calls
    don't run at the same time, which could be disasterous. A single
    semaphore could be used across malloc, realloc, and free. It would
    be hard to obtain finer granularity.


* Implementation notes

    (The following includes lightly edited explanations by Colin Plumb.)

    An allocated chunk looks like this:  


    chunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of chunk, in bytes                         |P|
      mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             User data starts here...                          .
            .                                                               .
            .             (malloc_usable_space() bytes)                     .
            .                                                               |
nextchunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of next chunk                              |1|
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


    Where "chunk" is the front of the chunk for the purpose of most of
    the malloc code, but "mem" is the pointer that is returned to the
    user.  "Nextchunk" is the beginning of the next contiguous chunk.

    Chunks always begin on odd-word boundries, so the mem portion
    (which is returned to the user) is on an even word boundary, and
    thus double-word aligned.

    Free chunks are stored in circular doubly-linked lists, and look like this:

    chunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of chunk, in bytes                         |P|
      mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Forward pointer to next chunk in list             |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Back pointer to previous chunk in list            |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Unused space (may be 0 bytes long)                .
            .                                                               .
            .                                                               |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of chunk, in bytes                           |
nextchunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of next chunk                              |0|
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    The P (PREV_INUSE) bit, stored in the unused low-order bit of the
    chunk size (which is always a multiple of two words), is an in-use
    bit for the *previous* chunk.  If that bit is *clear*, then the
    word before the current chunk size contains the previous chunk
    size, and can be used to find the front of the previous chunk.
    (The very first chunk allocated always has this bit set,
    preventing access to non-existent (or non-owned) memory.)

    The only exception to all this is the special chunk `top', which
    doesn't bother using the trailing size field since there is no
    next contiguous chunk that would have to index off it. (After
    initialization, `top' is forced to always exist.  If it would
    become less than MINSIZE bytes long, it is replenished via
    malloc_extend_top.)

    The bins, `av_' are an array of pointers serving as the heads of
    (initially empty) doubly-linked lists of chunks.  Bins for sizes <
    512 bytes contain chunks of all the same size, spaced 8 bytes
    apart. Larger bins are approximately logarithmically spaced. (See
    the table below.) The `av_' array is never mentioned directly 
    in the code, but instead via bin access macros.

    The chunks in each bin are linked in decreasing sorted order by
    size.  This is irrelevant for the small bins, which all contain
    the same-sized chunks, but facilitates best-fit allocation for
    larger chunks. (These lists are just sequential. Keeping them in
    order almost never requires enough traversal to warrant using
    fancier ordered data structures.)  Chunks of the same size are
    linked with the most recently freed at the front, and allocations
    are taken from the back.  This results in LRU or FIFO allocation
    order, which tends to give each chunk an equal opportunity to be
    consolidated with adjacent freed chunks, resulting in larger free
    chunks and less fragmentation. 

    The exception to this ordering is that freed chunks of size <=
    max_recycle_size are scanned in LIFO order (i.e., the most
    recently freed chunk is scanned first) and used if possible in
    malloc (or if not usable, placed into a normal FIFO bin). This
    ordering adapts better to size-phasing in user programs. The
    recycle_list that holds these chunks is a simple singly-linked
    list that uses the `fd' pointers of the chunks for linking.

    The special chunks `top' and `last_remainder' get their own bins,
    (this is implemented via yet more trickery with the av_ array),
    although `top' is never properly linked to its bin since it is
    always handled specially.

    Search is generally via best-fit; i.e., the smallest (with ties
    going to approximately the least recently used) chunk that fits is
    selected.  The use of `top' is in accord with this rule.  In
    effect, `top' is treated as larger (and thus less well fitting)
    than any other available chunk since it can be extended to be as
    large as necessary (up to system limitations).

    The exception to this search rule is that in the absence of exact
    fits, runs of same-sized (or merely `small') requests use the
    remainder of the chunk used for the previous such request whenever
    possible. This limited use of a `first-fit' style allocation
    strategy tends to give contiguous chunks coextensive lifetimes,
    which improves locality and sometimes reduces fragmentation in the
    long run.

    All allocations are made from the the `lowest' part of any found
    chunk. (The implementation invariant is that prev_inuse is always
    true of any allocated chunk; i.e., that each allocated chunk
    borders a previously allocated and still in-use chunk.) This
    policy holds even for chunks on the recycle_list. Recycled chunks
    that do not border used chunks are bypassed. (However, the policy
    holds only approximately in this case. A taken chunk might border
    one that is not really in use, but is instead still on the recycle
    list.)  This also tends to reduce fragmentation, improve locality,
    and increase the likelihood that malloc_trim will actually release
    memory.

    To help compensate for the large number of bins, a one-level index
    structure is used for bin-by-bin searching.  `binblocks' is a
    one-word bitvector recording whether groups of BINBLOCKWIDTH bins
    have any (possibly) non-empty bins, so they can be skipped over
    all at once during during traversals. The bits are NOT always
    cleared as soon as all bins in a block are empty, but instead only
    when all are noticed to be empty during traversal in malloc.

  * Style

    The implementation is in straight, hand-tuned ANSI C.  Among other
    consequences, it uses a lot of macros. These would be nicer as
    inlinable procedures, but using macros allows use with
    non-inlining compilers.  The use of macros etc., requires that, to
    be at all usable, this code be compiled using an optimizing
    compiler (for example gcc -O2) that can simplify expressions and
    control paths.  Also, because there are so many different twisty
    paths through malloc steps, the code is not exactly elegant.



* History:

    V2.6.2 Tue Dec  5 06:52:55 1995  Doug Lea  (dl at gee)
      * Re-introduce recycle_list, similar to `returns' list in V2.5.X.
      * Use last_remainder in more cases.
      * Pack bins using idea from  colin@nyx10.cs.du.edu
      * Use ordered bins instead of best-fit threshhold
      * Eliminate block-local decls to simplify tracing and debugging.
      * Support another case of realloc via move into top
      * Fix error occuring when initial sbrk_base not word-aligned.  
      * Rely on page size for units instead of SBRK_UNIT to
        avoid surprises about sbrk alignment conventions.
      * Add mallinfo, mallopt. Thanks to Raymond Nijssen
        (raymond@es.ele.tue.nl) for the suggestion. 
      * Add `pad' argument to malloc_trim and top_pad mallopt parameter.
      * More precautions for cases where other routines call sbrk,
        courtesy of Wolfram Gloger (Gloger@lrz.uni-muenchen.de).
      * Added macros etc., allowing use in linux libc from
        H.J. Lu (hjl@gnu.ai.mit.edu)
      * Inverted this history list

    V2.6.1 Sat Dec  2 14:10:57 1995  Doug Lea  (dl at gee)
      * Re-tuned and fixed to behave more nicely with V2.6.0 changes.
      * Removed all preallocation code since under current scheme
        the work required to undo bad preallocations exceeds
        the work saved in good cases for most test programs.
      * No longer use return list or unconsolidated bins since
        no scheme using them consistently outperforms those that don't
        given above changes.
      * Use best fit for very large chunks to prevent some worst-cases.
      * Added some support for debugging

    V2.6.0 Sat Nov  4 07:05:23 1995  Doug Lea  (dl at gee)
      * Removed footers when chunks are in use. Thanks to
        Paul Wilson (wilson@cs.texas.edu) for the suggestion.

    V2.5.4 Wed Nov  1 07:54:51 1995  Doug Lea  (dl at gee)
      * Added malloc_trim, with help from Wolfram Gloger 
        (wmglo@Dent.MED.Uni-Muenchen.DE).

    V2.5.3 Tue Apr 26 10:16:01 1994  Doug Lea  (dl at g)

    V2.5.2 Tue Apr  5 16:20:40 1994  Doug Lea  (dl at g)
      * realloc: try to expand in both directions
      * malloc: swap order of clean-bin strategy;
      * realloc: only conditionally expand backwards
      * Try not to scavenge used bins
      * Use bin counts as a guide to preallocation
      * Occasionally bin return list chunks in first scan
      * Add a few optimizations from colin@nyx10.cs.du.edu

    V2.5.1 Sat Aug 14 15:40:43 1993  Doug Lea  (dl at g)
      * faster bin computation & slightly different binning
      * merged all consolidations to one part of malloc proper
         (eliminating old malloc_find_space & malloc_clean_bin)
      * Scan 2 returns chunks (not just 1)
      * Propagate failure in realloc if malloc returns 0
      * Add stuff to allow compilation on non-ANSI compilers 
          from kpv@research.att.com
     
    V2.5 Sat Aug  7 07:41:59 1993  Doug Lea  (dl at g.oswego.edu)
      * removed potential for odd address access in prev_chunk
      * removed dependency on getpagesize.h
      * misc cosmetics and a bit more internal documentation
      * anticosmetics: mangled names in macros to evade debugger strangeness
      * tested on sparc, hp-700, dec-mips, rs6000 
          with gcc & native cc (hp, dec only) allowing
          Detlefs & Zorn comparison study (in SIGPLAN Notices.)

    Trial version Fri Aug 28 13:14:29 1992  Doug Lea  (dl at g.oswego.edu)
      * Based loosely on libg++-1.2X malloc. (It retains some of the overall 
         structure of old version,  but most details differ.)

*/



/* ---------- To make a malloc.h, start cutting here ------------ */

/* preliminaries */

#ifndef __STD_C
#ifdef __STDC__
#define __STD_C     1
#else
#if __cplusplus
#define __STD_C     1
#else
#define __STD_C     0
#endif /*__cplusplus*/
#endif /*__STDC__*/
#endif /*__STD_C*/

#ifndef Void_t
#if __STD_C
#define Void_t      void
#else
#define Void_t      char
#endif
#endif /*Void_t*/

#if __STD_C
#include <stddef.h>   /* for size_t */
#else
#include <sys/types.h>
#endif

#include <stdio.h>    /* needed for malloc_stats */

#if DEBUG             /* define DEBUG to get run-time debug assertions */
#include <assert.h>
#else
#define assert(x) ((void)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
  Compile-time options
*/

/*
  REALLOC_ZERO_BYTES_FREES should be set if a call to
  realloc with zero bytes should be the same as a call to free.
  Some people think it should. Otherwise, since this malloc
  returns a unique pointer for malloc(0), so does realloc(p, 0). 
*/


/*   #define REALLOC_ZERO_BYTES_FREES */


/*
  HAVE_MEMCPY should be defined if you are not otherwise using
  ANSI STD C, but still have memcpy and memset in your C library
  and want to use them. By default defined.
*/

#define HAVE_MEMCPY 

/* how to zero out and copy memory (needed in calloc, realloc) */

#if __STD_C || defined(HAVE_MEMCPY)

void* memset(void*, int, size_t);
void* memcpy(void*, const void*, size_t);

#define MALLOC_ZERO(charp, nbytes)  memset(charp, 0, nbytes)
#define MALLOC_COPY(dest,src,nbytes) memcpy((dest), (src), (nbytes))

#else

/* We only invoke with multiples of size_t units, with size_t alignment */

#define MALLOC_ZERO(charp, nbytes)                                            \
{                                                                             \
  size_t* mzp = (size_t*)(charp);                                             \
  size_t mzn = (nbytes) / sizeof(size_t);                                     \
  while (mzn-- > 0) *mzp++ = 0;                                               \
} 

#define MALLOC_COPY(dest,src,nbytes)                                          \
{                                                                             \
  size_t* mcsrc = (size_t*) src;                                              \
  size_t* mcdst = (size_t*) dest;                                             \
  long mcn = (nbytes) / sizeof(size_t);                                       \
  while (mcn-- > 0) *mcdst++ = *mcsrc++;                                      \
}

#endif


/*
  Define HAVE_MMAP to optionally make malloc() use mmap() to
  allocate very large blocks.  These will be returned to the
  operating system immediately after a free().
*/

#ifndef HAVE_MMAP
#define HAVE_MMAP 1
#endif

#if HAVE_MMAP

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

#endif /* HAVE_MMAP */

  
/*

  HAVE_USR_INCLUDE_MALLOC_H should be set if you have a
  /usr/include/malloc.h file that includes an SVID2/XPG2 declaration
  of struct mallinfo.  If so, it is included; else an SVID2/XPG2
  compliant version is declared within this file. Since these must be
  precisely the same for mallinfo and mallopt to work anyway, the main
  reason to define this would be to prevent multiple-declaration
  errors in files already including malloc.h.

*/

/* #define HAVE_USR_INCLUDE_MALLOC_H */

#if HAVE_USR_INCLUDE_MALLOC_H
#include "/usr/include/malloc.h"
#else

/* SVID2/XPG mallinfo structure */

struct mallinfo {
  int arena;    /* total space allocated from system */
  int ordblks;  /* number of non-inuse, non-recycling chunks */
  int smblks;   /* number of chunks in recycle list */
  int hblks;    /* unused -- always zero */
  int hblkhd;   /* unused -- always zero */
  int usmblks;  /* unused -- always zero */
  int fsmblks;  /* total space in recycle list */
  int uordblks; /* total allocated space */
  int fordblks; /* total non-inuse, non-recycling space */
  int keepcost; /* top-most, releasable (via malloc_trim) space */
};	

/* SVID2/XPG mallopt options */
#define M_MXFAST  1
#define M_NLBLKS  2
#define M_GRAIN   3
#define M_KEEP    4

#endif

/* mallopt options that actually do something */

#ifndef M_MXFAST
#define M_MXFAST  1
#endif

#define M_TRIM_THRESHOLD  -1
#define M_TOP_PAD         -2
#define M_MMAP_THRESHOLD  -3
#define M_MMAP_MAX        -4

/* 
  Initial values of tunable parameters 
*/

#ifndef DEFAULT_TRIM_THRESHOLD
#define DEFAULT_TRIM_THRESHOLD (256 * 1024)
#endif


#ifndef DEFAULT_TOP_PAD
#define DEFAULT_TOP_PAD        (2 * 1024)
#endif


#ifndef DEFAULT_RECYCLE_SIZE
#define DEFAULT_RECYCLE_SIZE   (72)
#endif


#ifndef DEFAULT_MMAP_THRESHOLD
#define DEFAULT_MMAP_THRESHOLD (512 * 1024)
#endif


#ifndef DEFAULT_MMAP_MAX
#define DEFAULT_MMAP_MAX       (16)
#endif


#ifdef INTERNAL_LINUX_C_LIB

#if __STD_C

Void_t * __default_morecore_init (ptrdiff_t);
Void_t *(*__morecore)(ptrdiff_t) = __default_morecore_init;

#else

Void_t * __default_morecore_init ();
Void_t *(*__morecore)() = __default_morecore_init;

#endif

#define MORECORE (*__morecore)
#define MORECORE_FAILURE 0

#else /* INTERNAL_LINUX_C_LIB */

#if __STD_C
extern Void_t*     sbrk(ptrdiff_t);
#else
extern Void_t*     sbrk();
#endif

#define MORECORE sbrk
#define MORECORE_FAILURE -1

#endif /* INTERNAL_LINUX_C_LIB */

#if defined(INTERNAL_LINUX_C_LIB) && defined(__ELF__)

#define CALLOC		__libc_calloc
#define FREE		__libc_free
#define MALLOC		__libc_malloc
#define MEMALIGN	__libc_memalign
#define REALLOC		__libc_realloc
#define VALLOC		__libc_valloc
#define MALLINFO	__libc_mallinfo
#define MALLOPT		__libc_mallopt

#pragma weak calloc = __libc_calloc
#pragma weak free = __libc_free
#pragma weak cfree = __libc_free
#pragma weak malloc = __libc_malloc
#pragma weak memalign = __libc_memalign
#pragma weak realloc = __libc_realloc
#pragma weak valloc = __libc_valloc
#pragma weak mallinfo = __libc_mallinfo
#pragma weak mallopt = __libc_mallopt

#else

#define CALLOC		calloc
#define FREE		free
#define MALLOC		malloc
#define MEMALIGN	memalign
#define REALLOC		realloc
#define VALLOC		valloc
#define MALLINFO	mallinfo
#define MALLOPT		mallopt

#endif

/* mechanics for getpagesize; adapted from bsd/gnu getpagesize.h */

#if defined(BSD) || defined(DGUX) || defined(HAVE_GETPAGESIZE)
   extern size_t getpagesize();
#  define malloc_getpagesize getpagesize()
#else
#  include <sys/param.h>
#  ifdef EXEC_PAGESIZE
#    define malloc_getpagesize EXEC_PAGESIZE
#  else
#    ifdef NBPG
#      ifndef CLSIZE
#        define malloc_getpagesize NBPG
#      else
#        define malloc_getpagesize (NBPG * CLSIZE)
#      endif
#    else 
#      ifdef NBPC
#        define malloc_getpagesize NBPC
#      else
#        ifdef PAGESIZE
#          define malloc_getpagesize PAGESIZE
#        else
#          define malloc_getpagesize (8192) /* just guess */
#        endif
#      endif
#    endif 
#  endif
#endif 


/* Declarations of public routines */

#if __STD_C
Void_t* MALLOC(size_t);
void    FREE(Void_t*);
Void_t* REALLOC(Void_t*, size_t);
Void_t* MEMALIGN(size_t, size_t);
Void_t* VALLOC(size_t);
Void_t* CALLOC(size_t, size_t);
void    cfree(Void_t*);
int     malloc_trim(size_t);
size_t  malloc_usable_size(Void_t*);
void    malloc_stats();
int     MALLOPT(int, int);
struct mallinfo MALLINFO(void);
#else
Void_t* MALLOC();
void    FREE();
Void_t* REALLOC();
Void_t* MEMALIGN();
Void_t* VALLOC();
Void_t* CALLOC();
void    cfree();
int     malloc_trim();
size_t  malloc_usable_size();
void    malloc_stats();
int     MALLOPT();
struct mallinfo MALLINFO();
#endif





#ifdef __cplusplus
};  /* end of extern "C" */
#endif

/* ---------- To make a malloc.h, end cutting here ------------ */


/*  CHUNKS */


struct malloc_chunk
{
  size_t size;               /* Size in bytes, including overhead. */
  struct malloc_chunk* fd;   /* double links -- used only if free. */
  struct malloc_chunk* bk;
  size_t unused;             /* to pad decl to min chunk size */
};

/* size field is or'ed with PREV_INUSE when previous adjacent chunk in use */

#define PREV_INUSE 0x1 

/* size field is or'ed with IS_MMAPPED if the chunk was obtained with mmap() */

#define IS_MMAPPED 0x2

typedef struct malloc_chunk* mchunkptr;

/*  sizes, alignments */

#define SIZE_SZ                (sizeof(size_t))
#define MALLOC_ALIGN_MASK      (SIZE_SZ + SIZE_SZ - 1)
#define MINSIZE                (sizeof(struct malloc_chunk))

/* pad request bytes into a usable size */

#define request2size(req) \
  (((long)(req) < (long)(MINSIZE - SIZE_SZ)) ?  MINSIZE : \
   (((req) + SIZE_SZ + MALLOC_ALIGN_MASK) & ~(MALLOC_ALIGN_MASK)))


/* Check if m has acceptable alignment */

#define aligned_OK(m)    (((size_t)((m)) & (MALLOC_ALIGN_MASK)) == 0)




/* 
  Physical chunk operations  
*/

/* Ptr to next physical malloc_chunk. */

#define next_chunk(p) ((mchunkptr)( ((char*)(p)) + ((p)->size & ~PREV_INUSE) ))

/* Ptr to previous physical malloc_chunk */

#define prev_chunk(p)\
   ((mchunkptr)( ((char*)(p)) - *((size_t*)((char*)(p) - SIZE_SZ))))


/* Treat space at ptr + offset as a chunk */

#define chunk_at_offset(p, s)  ((mchunkptr)(((char*)(p)) + (s)))

/* conversion from malloc headers to user pointers, and back */

#define chunk2mem(p)   ((Void_t*)((char*)(p) + SIZE_SZ))
#define mem2chunk(mem) ((mchunkptr)((char*)(mem) - SIZE_SZ))



/* 
  Dealing with use bits 
*/

/* extract p's inuse bit */

#define inuse(p)\
((((mchunkptr)(((char*)(p))+((p)->size & ~PREV_INUSE)))->size) & PREV_INUSE)

/* extract inuse bit of previous chunk */

#define prev_inuse(p)  ((p)->size & PREV_INUSE)

/* check for mmap()'ed chunk */

#define chunk_is_mmapped(p) ((p)->size & IS_MMAPPED)

/* set/clear chunk as in use without otherwise disturbing */

#define set_inuse(p)\
((mchunkptr)(((char*)(p)) + ((p)->size & ~PREV_INUSE)))->size |= PREV_INUSE

#define clear_inuse(p)\
((mchunkptr)(((char*)(p)) + ((p)->size & ~PREV_INUSE)))->size &= ~(PREV_INUSE)

/* check/set/clear inuse bits in known places */

#define inuse_bit_at_offset(p, s)\
 (((mchunkptr)(((char*)(p)) + (s)))->size & PREV_INUSE)

#define set_inuse_bit_at_offset(p, s)\
 (((mchunkptr)(((char*)(p)) + (s)))->size |= PREV_INUSE)

#define clear_inuse_bit_at_offset(p, s)\
 (((mchunkptr)(((char*)(p)) + (s)))->size &= ~(PREV_INUSE))






/* 
  Dealing with size fields 
*/

/* Get size, ignoring use bits */

#define chunksize(p)          ((p)->size & ~(PREV_INUSE|IS_MMAPPED))

/* Set size at head, without disturbing its use bit */

#define set_head_size(p, s)   ((p)->size = (((p)->size & PREV_INUSE) | (s)))

/* Set size/use ignoring previous bits in header */

#define set_head(p, s)        ((p)->size = (s))

/* Set size at footer (only when chunk is not in use) */

#define set_foot(p, s)   (*((size_t*)((char*)(p) + (s) - SIZE_SZ)) = (s))

/* Get size of previous (but not inuse) chunk */

#define prev_size(p)          (*((size_t*)((char*)(p) - SIZE_SZ)))




/*
   Bins and related static data
*/

/* 
   The bins are just an array of list headers, arranged in
   a way so that they can always be coerced into malloc_chunks
   (so long as the size fields aren't ever accessed).
*/

typedef struct malloc_chunk* mbinptr;

#define bin_at(i)      ((mbinptr)(&(av_[2 * (i)])))
#define next_bin(b)    ((mbinptr)((char*)(b) + 2 * sizeof(mbinptr)))

#define NAV             128   /* number of bins */
#define BINBLOCKWIDTH     4   /* bins per block */

/*
   The first 2 bins are never indexed. The corresponding av_ cells are instead
   used for bookkeeping. This is not to save space, but to simplify
   indexing, maintain locality, and avoid some initialization tests.
*/


#define binblocks      (bin_at(0)->size) /* bitvector of nonempty blocks */

#define top            (bin_at(0)->fd)   /* The topmost chunk */

/*
   Because top initially points to its own bin with initially
   zero size, thus forcing extension on the first malloc request, 
   we avoid having any special code in malloc to check whether 
   it even exists yet. But we still need to in malloc_extend_top.
*/

#define initial_top    ((mchunkptr)(av_)) 


#define last_remainder (bin_at(1))       /* remainder from last split */
/* (Even though overlaps with bin_at(0), size field of bin_at(1) is usable) */


/* Helper macro to initialize bins */

#define IAV(i)  (mbinptr)(av_ + 2 * i), (mbinptr)(av_ + 2 * i)

static mbinptr av_[NAV * 2 + 4] = {
       0, (mbinptr)(av_), 0, 
            IAV(1),   IAV(2),   IAV(3),   IAV(4), 
  IAV(5),   IAV(6),   IAV(7),   IAV(8),   IAV(9),
  IAV(10),  IAV(11),  IAV(12),  IAV(13),  IAV(14), 
  IAV(15),  IAV(16),  IAV(17),  IAV(18),  IAV(19),
  IAV(20),  IAV(21),  IAV(22),  IAV(23),  IAV(24), 
  IAV(25),  IAV(26),  IAV(27),  IAV(28),  IAV(29),
  IAV(30),  IAV(31),  IAV(32),  IAV(33),  IAV(34), 
  IAV(35),  IAV(36),  IAV(37),  IAV(38),  IAV(39),
  IAV(40),  IAV(41),  IAV(42),  IAV(43),  IAV(44), 
  IAV(45),  IAV(46),  IAV(47),  IAV(48),  IAV(49),
  IAV(50),  IAV(51),  IAV(52),  IAV(53),  IAV(54), 
  IAV(55),  IAV(56),  IAV(57),  IAV(58),  IAV(59),
  IAV(60),  IAV(61),  IAV(62),  IAV(63),  IAV(64), 
  IAV(65),  IAV(66),  IAV(67),  IAV(68),  IAV(69),
  IAV(70),  IAV(71),  IAV(72),  IAV(73),  IAV(74), 
  IAV(75),  IAV(76),  IAV(77),  IAV(78),  IAV(79),
  IAV(80),  IAV(81),  IAV(82),  IAV(83),  IAV(84), 
  IAV(85),  IAV(86),  IAV(87),  IAV(88),  IAV(89),
  IAV(90),  IAV(91),  IAV(92),  IAV(93),  IAV(94), 
  IAV(95),  IAV(96),  IAV(97),  IAV(98),  IAV(99),
  IAV(100), IAV(101), IAV(102), IAV(103), IAV(104), 
  IAV(105), IAV(106), IAV(107), IAV(108), IAV(109),
  IAV(110), IAV(111), IAV(112), IAV(113), IAV(114), 
  IAV(115), IAV(116), IAV(117), IAV(118), IAV(119),
  IAV(120), IAV(121), IAV(122), IAV(123), IAV(124), 
  IAV(125), IAV(126), IAV(127), 0, 0, 0
};


/* Other static bookkeeping data */

/* The list of recycled chunks */
static mchunkptr recycle_list = 0;

/* variables holding tunable values */

static unsigned long max_recycle_size = DEFAULT_RECYCLE_SIZE;
static unsigned long trim_threshold =   DEFAULT_TRIM_THRESHOLD;
static unsigned long top_pad        =   DEFAULT_TOP_PAD;

/* The first value returned from sbrk */
static char* sbrk_base = (char*)(-1);

/* The maximum memory obtained from system via sbrk */
static size_t max_sbrked_mem = 0; 

/* internal working copy of mallinfo */
static struct mallinfo current_mallinfo = {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* The total memory obtained from system via sbrk */
#define sbrked_mem  (current_mallinfo.arena)





/* 
  Operations on bins and bin lists
*/


/* 
  Indexing into bins

  Bins are log-spaced:

  64 bins of size       8
  32 bins of size      64
  16 bins of size     512
   8 bins of size    4096
   4 bins of size   32768
   2 bins of size  262144
   1 bin  of size what's left

  There is actually a little bit of slop in the numbers in bin_index
  for the sake of speed. This makes no difference elsewhere.
*/

#define bin_index(sz)                                                          \
(((((unsigned long)(sz)) >> 9) ==    0) ?       (((unsigned long)(sz)) >>  3): \
 ((((unsigned long)(sz)) >> 9) <=    4) ?  56 + (((unsigned long)(sz)) >>  6): \
 ((((unsigned long)(sz)) >> 9) <=   20) ?  91 + (((unsigned long)(sz)) >>  9): \
 ((((unsigned long)(sz)) >> 9) <=   84) ? 110 + (((unsigned long)(sz)) >> 12): \
 ((((unsigned long)(sz)) >> 9) <=  340) ? 119 + (((unsigned long)(sz)) >> 15): \
 ((((unsigned long)(sz)) >> 9) <= 1364) ? 124 + (((unsigned long)(sz)) >> 18): \
                                          126)                     
/* 
  bins for chunks < 512 are all spaced 8 bytes apart, and hold
  identically sized chunks. This is exploited in malloc.
*/

#define MAX_SMALLBIN         63
#define MAX_SMALLBIN_SIZE   512
#define SMALLBIN_WIDTH        8

#define smallbin_index(sz)  (((unsigned long)(sz)) >> 3)


/* 
   Requests are `small' if both the corresponding and the next bin are small
*/

#define is_small_request(nb) (nb < MAX_SMALLBIN_SIZE - SMALLBIN_WIDTH)



/* field-extraction macros */

#define first(b) ((b)->fd)
#define last(b)  ((b)->bk)


/* bin<->block macros */

#define idx2binblock(ix)    (1 << (ix / BINBLOCKWIDTH))
#define mark_binblock(ii)   (binblocks |= idx2binblock(ii))
#define clear_binblock(ii)  (binblocks &= ~(idx2binblock(ii)))


/*  
  Linking macros.
  Call these only with variables, not arbitrary expressions, as arguments.
  (Currently, each of the 3 linking macros is used only once in the rest of
  the code.)
*/

/* 
  Place chunk p of size s in its bin, in size order,
  putting it ahead of others of same size.
*/

#define frontlink(P, S, IDX, BK, FD)                                          \
  if (S < MAX_SMALLBIN_SIZE)                                                  \
  {                                                                           \
    IDX = smallbin_index(S);                                                  \
    mark_binblock(IDX);                                                       \
    BK = bin_at(IDX);                                                         \
    FD = BK->fd;                                                              \
    P->bk = BK;                                                               \
    P->fd = FD;                                                               \
    FD->bk = BK->fd = P;                                                      \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    IDX = bin_index(S);                                                       \
    BK = bin_at(IDX);                                                         \
    FD = BK->fd;                                                              \
    if (FD == BK) mark_binblock(IDX);                                         \
    else                                                                      \
    {                                                                         \
      while (FD != BK && S < chunksize(FD)) FD = FD->fd;                      \
      BK = FD->bk;                                                            \
    }                                                                         \
    P->bk = BK;                                                               \
    P->fd = FD;                                                               \
    FD->bk = BK->fd = P;                                                      \
  }

/* Simplified version for known small chunks */

#define smallfrontlink(P, S, IDX, BK, FD)                                     \
{                                                                             \
  IDX = smallbin_index(S);                                                    \
  mark_binblock(IDX);                                                         \
  BK = bin_at(IDX);                                                           \
  FD = BK->fd;                                                                \
  P->bk = BK;                                                                 \
  P->fd = FD;                                                                 \
  FD->bk = BK->fd = P;                                                        \
}


/* Same, except start at back instead of front -- used for known old chunks */

#define backlink(P, S, IDX, BK, FD)                                           \
{                                                                             \
  IDX = bin_index(S);                                                         \
  FD = bin_at(IDX);                                                           \
  BK = FD->bk;                                                                \
  if (FD == BK)   mark_binblock(IDX);                                         \
  else                                                                        \
  {                                                                           \
    while (FD != BK && S > BK->size) BK = BK->bk;                             \
    FD = BK->fd;                                                              \
  }                                                                           \
  P->bk = BK;                                                                 \
  P->fd = FD;                                                                 \
  FD->bk = BK->fd = P;                                                        \
}



/* take a chunk off a list */

#define unlink(P, BK, FD)                                                     \
{                                                                             \
  BK = P->bk;                                                                 \
  FD = P->fd;                                                                 \
  FD->bk = BK;                                                                \
  BK->fd = FD;                                                                \
}                                                                             \

/* Place p as the last remainder */

#define link_last_remainder(P)                                                \
{                                                                             \
  last_remainder->fd = last_remainder->bk =  P;                               \
  P->fd = P->bk = last_remainder;                                             \
}

/* Clear the last_remainder bin */

#define clear_last_remainder \
  (last_remainder->fd = last_remainder->bk = last_remainder)




/* 
  Debugging support 
*/

#if DEBUG


/*
  These routines make a number of assertions about the states
  of data structures that should be true at all times. If any
  are not true, it's very likely that a user program has somehow
  trashed memory. (It's also possible that there is a coding error
  in malloc. In which case, please report it!)
*/

#if __STD_C
static void do_check_chunk(mchunkptr p) 
#else
static void do_check_chunk(p) mchunkptr p;
#endif
{ 
  size_t sz = p->size & ~PREV_INUSE;

  /* Check for legal address ... */
  assert((char*)p >= sbrk_base);
  if (p != top) 
    assert((char*)p + sz <= (char*)top);
  else
    assert((char*)p + sz <= sbrk_base + sbrked_mem);
}


#if __STD_C
static void do_check_free_chunk(mchunkptr p) 
#else
static void do_check_free_chunk(p) mchunkptr p;
#endif
{ 
  size_t sz = p->size & ~PREV_INUSE;
  mchunkptr next = chunk_at_offset(p, sz);

  do_check_chunk(p);

  /* Check whether it claims to be free ... */
  assert(!inuse(p));

  /* Unless a special marker, must have OK fields */
  if ((long)sz >= (long)MINSIZE)
  {
    assert((sz & MALLOC_ALIGN_MASK) == 0);
    assert((((size_t)((char*)(p) + SIZE_SZ)) & MALLOC_ALIGN_MASK) == 0);
    /* ... matching footer field */
    assert(*((size_t*)((char*)(p) + sz - SIZE_SZ)) == sz);
    /* ... and is fully consolidated */
    assert(prev_inuse(p));
    assert (next == top || inuse(next));
    
    /* ... and has minimally sane links */
    assert(p->fd->bk == p);
    assert(p->bk->fd == p);
  }
  else /* markers are always of size SIZE_SZ */
    assert(sz == SIZE_SZ); 
}

#if __STD_C
static void do_check_inuse_chunk(mchunkptr p) 
#else
static void do_check_inuse_chunk(p) mchunkptr p;
#endif
{ 
  mchunkptr next = next_chunk(p);
  do_check_chunk(p);

  /* Check whether it claims to be in use ... */
  assert(inuse(p));

  /* ... and is surrounded by OK chunks.
    Since more things can be checked with free chunks than inuse ones,
    if an inuse chunk borders them and debug is on, it's worth doing them.
  */
  if (!prev_inuse(p)) 
  {
    mchunkptr prv = prev_chunk(p);
    assert(next_chunk(prv) == p);
    do_check_free_chunk(prv);
  }
  if (next == top)
    assert(prev_inuse(next));
  else if (!inuse(next))
    do_check_free_chunk(next);

}

#if __STD_C
static void do_check_malloced_chunk(mchunkptr p, size_t s) 
#else
static void do_check_malloced_chunk(p, s) mchunkptr p; size_t s;
#endif
{
  size_t sz = p->size & ~PREV_INUSE;
  long room = sz - s;

  do_check_inuse_chunk(p);

  /* Legal size ... */
  assert((long)sz >= (long)MINSIZE);
  assert((sz & MALLOC_ALIGN_MASK) == 0);
  assert(room >= 0);
  assert(room < (long)MINSIZE);

  /* ... and alignment */
  assert((((size_t)((char*)(p) + SIZE_SZ)) & MALLOC_ALIGN_MASK) == 0);


  /* ... and was allocated at front of an available chunk */
  assert(prev_inuse(p));

}


#define check_free_chunk(P)  do_check_free_chunk(P)
#define check_inuse_chunk(P) do_check_inuse_chunk(P)
#define check_chunk(P) do_check_chunk(P)
#define check_malloced_chunk(P,N) do_check_malloced_chunk(P,N)
#else
#define check_free_chunk(P) 
#define check_inuse_chunk(P)
#define check_chunk(P)
#define check_malloced_chunk(P,N)
#endif



/* Utility: Extend the top-most chunk by obtaining memory from system */

#if __STD_C
static void malloc_extend_top(size_t nb)
#else
static void malloc_extend_top(nb) size_t nb;
#endif
{
  char*     brk;                 /* return value from sbrk */
  size_t    front_misalign;      /* unusable bytes at front of sbrked space */
  size_t    correction;          /* bytes for 2nd sbrk call */
  char*     new_brk;             /* return of 2nd sbrk call */
  size_t    top_size;            /* new size of top chunk */

  mchunkptr old_top      = top;  /* Record state of old top */
  size_t    old_top_size = chunksize(old_top);
  char*     old_end      = (char*)(chunk_at_offset(old_top, old_top_size));

  /* Pad request with top_pad plus minimal overhead */
  
  size_t    sbrk_size     = nb + top_pad + MINSIZE;
  unsigned long pagesz    = malloc_getpagesize;

  /* If not the first time through, round to preserve page boundary */
  /* Otherwise, we need to correct to a page size below anyway. */
  /* (We also correct below if an intervening foreign sbrk call.) */

  if (sbrk_base != (char*)(-1))
    sbrk_size = ((sbrk_size + (pagesz -1)) / pagesz) * pagesz;

  brk = (char*)(MORECORE (sbrk_size));

  /* Fail if sbrk failed or if a foreign sbrk call killed our space */
  if (brk == (char*)(MORECORE_FAILURE) || 
      (brk < old_end && old_top != initial_top))
    return;     

  sbrked_mem += sbrk_size;

  if (brk == old_end) /* can just add bytes to current top */
  {
    top_size = sbrk_size + chunksize(top);
    set_head(top, top_size | PREV_INUSE);
  }
  else
  {
    if (sbrk_base == (char*)(-1))  /* First time through. Record base */
      sbrk_base = brk;
    else  /* Someone else called sbrk().  Count those bytes as sbrked_mem. */
      sbrked_mem += brk - (char*)old_end;

    /* Guarantee alignment of first new chunk made from this space */
    front_misalign = (size_t)chunk2mem(brk) & MALLOC_ALIGN_MASK;
    if (front_misalign > 0) 
    {
      correction = (MALLOC_ALIGN_MASK + 1) - front_misalign;
      brk += correction;
    }
    else
      correction = 0;

    /* Guarantee the next brk will be at a page boundary */
    correction += pagesz - ((size_t)(brk + sbrk_size) & (pagesz - 1));

    /* Allocate correction */
    new_brk = (char*)(MORECORE (correction));
    if (new_brk == (char*)(MORECORE_FAILURE)) return; 

    sbrked_mem += correction;

    top = (mchunkptr)brk;
    top_size = new_brk - brk + correction;
    set_head(top, top_size | PREV_INUSE);

    if (old_top != initial_top)
    {
      /* There must have been an intervening foreign sbrk call. */
      /* A double fencepost is necessary to prevent consolidation */
      chunk_at_offset(old_top, old_top_size - 2*SIZE_SZ)->size = 
        SIZE_SZ|PREV_INUSE;
      chunk_at_offset(old_top, old_top_size -   SIZE_SZ)->size = 
        SIZE_SZ|PREV_INUSE;

      /* Also keep size a multiple of MINSIZE */
      old_top_size = (old_top_size - 2*SIZE_SZ) & MALLOC_ALIGN_MASK;
      chunk_at_offset(old_top, old_top_size  )->size = SIZE_SZ|PREV_INUSE;
      chunk_at_offset(old_top, old_top_size+1)->size = SIZE_SZ|PREV_INUSE;
      set_head_size(old_top, old_top_size);
      /* If possible, release the rest of it via free. */
      if (old_top_size >= MINSIZE) 
        FREE(chunk2mem(old_top));
    }
  }

  if (sbrked_mem > max_sbrked_mem) max_sbrked_mem = sbrked_mem;

  /* We always land on a page boundary */
  assert(((size_t)((char*)top + top_size) & (pagesz - 1)) == 0);
}


#if HAVE_MMAP

static unsigned int n_mmaps = 0, n_mmaps_max = DEFAULT_MMAP_MAX;
static size_t mmap_threshold = DEFAULT_MMAP_THRESHOLD;

/* Routines dealing with mmap(). */

static mchunkptr
mmap_chunk(size_t size)
{
    size_t offset = (MALLOC_ALIGN_MASK+1) - SIZE_SZ;
    size_t page_mask = malloc_getpagesize - 1;
    char *cp;
    mchunkptr p;
#ifndef MAP_ANONYMOUS
    static int fd = -1;
#endif

    if(n_mmaps >= n_mmaps_max) return 0; /* too many regions */

    /* The offset to the start of the mmapped region is stored
     * in a size_t field immediately before the chunk.
     */
    size = (size + offset + page_mask) & ~page_mask;
#ifdef MAP_ANONYMOUS
    cp = (char *)mmap(0, size, PROT_READ|PROT_WRITE,
		      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
#else /* !MAP_ANONYMOUS */
    if(fd < 0) {
	fd = open("/dev/zero", O_RDWR);
	if(fd < 0) return 0;
    }
    cp = (char *)mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
#endif
    if(cp == (char *)-1) return 0;
    n_mmaps++;
    p = (mchunkptr)(cp + offset);
    assert(aligned_OK(chunk2mem(p)));
    *((size_t *)p - 1) = offset;
    set_head(p, (size - offset)|IS_MMAPPED);
    return p;
}

static void
munmap_chunk(mchunkptr p)
{
    size_t offset = *((size_t *)p - 1);
    size_t size = chunksize(p);

    assert((n_mmaps > 0) && chunk_is_mmapped(p));
    assert(((size + offset) & (malloc_getpagesize-1)) == 0);
    munmap((char *)p - offset, size + offset);
    n_mmaps--;
}

#endif /* HAVE_MMAP */





#if __STD_C
Void_t* MALLOC(size_t bytes)
#else
Void_t* MALLOC(bytes) size_t bytes;
#endif
{
  mchunkptr victim;                  /* inspected/selected chunk */
  size_t    victim_size;             /* its size */
  int       idx;                     /* index for bin traversal */
  mbinptr   bin;                     /* associated bin */
  mchunkptr remainder;               /* remainder from a split */
  long      remainder_size;          /* its size */
  int       remainder_index;         /* its bin index */
  unsigned long block;               /* block traverser bit */
  int       startidx;                /* first bin of a traversed block */
  mchunkptr fwd;                     /* misc temp for linking */
  mchunkptr bck;                     /* misc temp for linking */
  mchunkptr next;                    /* next contig chunk */
  size_t    nextsz;                  /* its size */
  size_t    prevsz;                  /* size of prev contig chunk */

  mchunkptr rl = recycle_list;

  size_t nb  = request2size(bytes);  /* padded request size; */

  /* Peek at recycle_list */
  if (rl != 0 &&
      prev_inuse(rl) &&
      (unsigned long)(rl->size - nb) < MINSIZE)
  {
    recycle_list = rl->fd;
    check_malloced_chunk(rl, nb);
    return chunk2mem(rl);
  }

  if (is_small_request(nb)) 
  {
    /* Check for exact match in a bin */

    idx = smallbin_index(nb); 

    /* No traversal or size check necessary for small bins.  */
    /* Also check the next one, since it would have a remainder < MINSIZE */

    if ( ((victim = last(bin_at(idx)))   != bin_at(idx)) || 
         ((victim = last(bin_at(idx+1))) != bin_at(idx+1)))
    {
      victim_size = chunksize(victim);
      unlink(victim, bck, fwd);
      set_inuse_bit_at_offset(victim, victim_size);
      check_malloced_chunk(victim, nb);
      return chunk2mem(victim);
    }

    idx += 2; /* Set for bin scan below -- we've already scanned 2 bins */

  }
  else
  {
#if HAVE_MMAP
    if (nb >= mmap_threshold)
    {
      victim = mmap_chunk(nb);
      if(victim) return chunk2mem(victim);
    }
#endif
    idx = bin_index(nb);
  }

  /* Use or consolidate freed chunks */

  if (rl != 0)
  {
    do
    {
      victim = rl;
      rl = rl->fd;
      victim_size = chunksize(victim);
      next = chunk_at_offset(victim, victim_size);
      nextsz = chunksize(next);
      
      if (prev_inuse(victim) && 
          next != top &&
          (unsigned long)(victim_size - nb) < MINSIZE)
      {
        recycle_list = rl;
        check_malloced_chunk(victim, nb);
        return chunk2mem(victim);
      }
      else if (next == top)                     /* merge with top */
      {
        victim_size += nextsz;
        if (!prev_inuse(victim))                /* consolidate backward */
        {
          prevsz = prev_size(victim);
          victim = chunk_at_offset(victim, -prevsz);
          unlink(victim, bck, fwd);
          victim_size += prevsz;
        }
        
        set_head(victim, victim_size | PREV_INUSE);
        top = victim;
      }
      else
      {
        set_head(next, nextsz);            /* clear inuse bit */
        
        if (!prev_inuse(victim))                /* consolidate backward */
        {
          prevsz = prev_size(victim);
          victim = chunk_at_offset(victim, -prevsz);
          victim_size += prevsz;
          if (victim->fd != last_remainder)    /* else keep as last_remainder */
            unlink(victim, bck, fwd);
        }
        
        if (!(inuse_bit_at_offset(next, nextsz)))   /* consolidate forward */
        {
          victim_size += nextsz;
          if (next->fd != last_remainder) 
          {
            unlink(next, bck, fwd);
          }
          else                              /* re-insert as last_remainder */
            link_last_remainder(victim);   
        }
        
        set_head(victim, victim_size | PREV_INUSE);
        set_foot(victim, victim_size);
        if (victim->fd != last_remainder)
          frontlink(victim, victim_size, remainder_index, bck, fwd);  
      }
    } while (rl != 0);
    recycle_list = 0;
  }

  /* For non-small requests, check own bin only after processing recycle_list */

  if (!is_small_request(nb))
  {
    bin = bin_at(idx);

    if ( (victim = last(bin)) != bin)
    {
      do
      {
        victim_size = chunksize(victim);
        remainder_size = victim_size - nb;
        if (remainder_size >= 0)
        {
          if (remainder_size < (long)MINSIZE)
          {
            unlink(victim, bck, fwd);
            set_inuse_bit_at_offset(victim, victim_size);
            check_malloced_chunk(victim, nb);
            return chunk2mem(victim);
          }
          else 
            break; /* (Will rescan below after checking last remainder) */
        }
      }
      while ( (victim = victim->bk) != bin);
      if (remainder_size < 0) ++idx; /* Don't rescan below */
    }
    else
      ++idx;
  }

  /* Try to use the last split-off remainder */

  if ( (victim = last_remainder->fd) != last_remainder)
  {
    victim_size = chunksize(victim);
    remainder_size = victim_size - nb;

    /* 
       Take if 
         * an exact fit
         * a consecutive small request 
         * a consecutive request of same size that caused remainder 
           to be split. (This size is kept in last_remainder->size.)
    */

    if (remainder_size >= 0 && remainder_size < (long)MINSIZE)
    {
      clear_last_remainder;
      set_inuse_bit_at_offset(victim, victim_size);
      check_malloced_chunk(victim, nb);
      return chunk2mem(victim);
    }

    else if (remainder_size >= 0 && 
             (is_small_request(nb) || nb == last_remainder->size))
    {
      remainder = chunk_at_offset(victim, nb);
      link_last_remainder(remainder);
      set_head(remainder, remainder_size | PREV_INUSE);
      set_foot(remainder, remainder_size);
      set_head(victim, nb | PREV_INUSE);
      check_malloced_chunk(victim, nb);
      return chunk2mem(victim);
    }
    
    else /* Place in bin */
    {
      clear_last_remainder;

      /* Put small ones in front of their bins so they can get bigger */
      if (victim_size < MAX_SMALLBIN_SIZE) 
      {
        smallfrontlink(victim, victim_size, remainder_index, bck, fwd);
      }
      /* bias others toward back to find again soon */
      else
      {
        backlink(victim, victim_size, remainder_index, bck, fwd);
      }
    }
  }

  /* If there are any possibly nonempty big-enough blocks, */
  /* search for best fitting chunk by scanning bins in blockwidth units */

  if ( (block = idx2binblock(idx)) <= binblocks)  
  {

    /* Get to the first marked block */
    if ( (block & binblocks) == 0) 
    {
      /* force to an even block boundary */
      idx = (idx & ~(BINBLOCKWIDTH - 1)) + BINBLOCKWIDTH;
      block <<= 1;
      while ((block & binblocks) == 0)
      {
        idx += BINBLOCKWIDTH;
        block <<= 1;
      }
    }
      
    /* For each possibly nonempty block ... */
    for (;;)  
    {
      startidx = idx;          /* (track incomplete blocks) */

      /* For each bin in this block ... */
      do           
      { 
        bin = bin_at(idx);

        /* Take first big enough chunk ... */

        for (victim = last(bin); victim != bin; victim = victim->bk)
        {
          victim_size = chunksize(victim);
          remainder_size = victim_size - nb;

          if (remainder_size >= 0)
          {
            unlink(victim, bck, fwd);
            
            if (remainder_size < (long)MINSIZE)  /* exact fit */
              set_inuse_bit_at_offset(victim, victim_size);
            
            else    /* place remainder in last_remainder */
            {
              remainder = chunk_at_offset(victim, nb);
              last_remainder->size = nb;
              link_last_remainder(remainder);
              set_head(remainder, remainder_size | PREV_INUSE);
              set_foot(remainder, remainder_size);
              set_head(victim, nb | PREV_INUSE);
            }
            check_malloced_chunk(victim, nb);
            return chunk2mem(victim);
          }
        }
      } while ((++idx & (BINBLOCKWIDTH - 1)) != 0);

      /* Clear out the block bit. */
      /* Possibly backtrack to try to clear a partial block */
      do
      {
        if ((startidx & (BINBLOCKWIDTH - 1)) == 0)
        {
          binblocks &= ~block;
          break;
        }
        --startidx;
      } while (first(bin_at(startidx)) == bin_at(startidx));

      /* Get to the next possibly nonempty block */
      if ( (block <<= 1) <= binblocks && (block != 0) ) 
      {
        while ((block & binblocks) == 0)
        {
          idx += BINBLOCKWIDTH;
          block <<= 1;
        }
      }
      else
        break;
    }
  }

  /* If fall though, use top chunk */

  victim = top;
  remainder_size = chunksize(victim) - nb;

  /* Require that there be a remainder (simplifies other processing)  */
  if (remainder_size < (long)MINSIZE)
  {
    malloc_extend_top(nb);
    victim = top;
    remainder_size = chunksize(victim) - nb;
    if (remainder_size < (long)MINSIZE) /* Propagate failure */
      return 0;
  }

  top = chunk_at_offset(victim, nb);
  set_head(top, remainder_size | PREV_INUSE);
  set_head(victim, nb | PREV_INUSE);
  check_malloced_chunk(victim, nb);
  return chunk2mem(victim);
}



#if __STD_C
void FREE(Void_t* mem)
#else
void FREE(mem) Void_t* mem;
#endif
{
  mchunkptr p;        /* chunk corresponding to mem */
  size_t    sz;       /* its size */
  int       idx;      /* its bin index */
  mchunkptr next;     /* next contiguous chunk */
  size_t    nextsz;   /* its size */
  size_t    prevsz;   /* size of previous contiguous chunk */
  mchunkptr bck;      /* misc temp for linking */
  mchunkptr fwd;      /* misc temp for linking */

  if (mem != 0)                        /* free(0) has no effect */
  {
    p = mem2chunk(mem);

#if HAVE_MMAP
    if(chunk_is_mmapped(p)) {
	/* Special case: mmap'ed memory. */
	munmap_chunk(p);
	return;
    }
#endif

    check_inuse_chunk(p);

    sz = chunksize(p);

    /* If small, place on the recycle_list for later processing */

    if (sz <= max_recycle_size)
    {
      p->fd = recycle_list;
      recycle_list = p;
      return;
    }
    
    next = chunk_at_offset(p, sz);
    nextsz = chunksize(next);

    if (next == top)                     /* merge with top */
    {
      sz += nextsz;
      if (!prev_inuse(p))                /* consolidate backward */
      {
        prevsz = prev_size(p);
        p = chunk_at_offset(p, -prevsz);
        unlink(p, bck, fwd);
        sz += prevsz;
      }

      set_head(p, sz | PREV_INUSE);
      top = p;
      /* If top is too big, call malloc_trim  */
      if ((unsigned long)sz >= (unsigned long)trim_threshold) 
        malloc_trim(top_pad); 
    }
    else
    {
      set_head(next, nextsz);            /* clear inuse bit for p */

      if (!prev_inuse(p))                /* consolidate backward */
      {
        prevsz = prev_size(p);
        p = chunk_at_offset(p, -prevsz);
        sz += prevsz;
        if (p->fd == last_remainder)    /* leave intact as last_remainder */
        {
          if (!(inuse_bit_at_offset(next, nextsz))) /* add forward */
          {
            unlink(next, bck, fwd);
            sz += nextsz;
          }
          set_head(p, sz | PREV_INUSE);
          set_foot(p, sz);
          return;
        }
        else
          unlink(p, bck, fwd);
      }

      if (!(inuse_bit_at_offset(next, nextsz)))   /* consolidate forward */
      {
        sz += nextsz;
        if (next->fd == last_remainder) /* re-insert as last_remainder */
        {
          link_last_remainder(p);   
          set_head(p, sz | PREV_INUSE);
          set_foot(p, sz);
          return;
        }
        else
          unlink(next, bck, fwd);
      }

      set_head(p, sz | PREV_INUSE);
      set_foot(p, sz);
      frontlink(p, sz, idx, bck, fwd);  
    }
  }
}



/* 
   Internal version of malloc/free consolidation routines
   used in realloc and malloc_trim.  (Sadly enough, the
   almost identical, but special cased versions in malloc and free
   are enough faster to justify redundancy.)
*/

static void process_recycle_list()
{
  mchunkptr p;
  size_t sz;
  mchunkptr next;
  size_t nextsz;
  size_t prevsz;
  int idx;
  mchunkptr fwd;
  mchunkptr bck;

  while (recycle_list != 0)
  {
    p = recycle_list;
    recycle_list = recycle_list->fd;
    sz = chunksize(p);

    next = chunk_at_offset(p, sz);
    nextsz = chunksize(next);

    if (next == top)                     /* merge with top */
    {
      sz += nextsz;
      if (!prev_inuse(p))                /* consolidate backward */
      {
        prevsz = prev_size(p);
        p = chunk_at_offset(p, -prevsz);
        unlink(p, bck, fwd);
        sz += prevsz;
      }

      set_head(p, sz | PREV_INUSE);
      top = p;
    }
    else
    {
      set_head(next, nextsz);            /* clear inuse bit for p */

      if (!prev_inuse(p))                /* consolidate backward */
      {
        prevsz = prev_size(p);
        p = chunk_at_offset(p, -prevsz);
        sz += prevsz;
        if (p->fd != last_remainder)    /* else leave as last_remainder */
          unlink(p, bck, fwd);
      }

      if (!(inuse_bit_at_offset(next, nextsz)))   /* consolidate forward */
      {
        sz += nextsz;
        if (next->fd != last_remainder) /* re-insert as last_remainder */
        {
          unlink(next, bck, fwd);
        }
        else
          link_last_remainder(p);   
      }

      set_head(p, sz | PREV_INUSE);
      set_foot(p, sz);
      if (p->fd != last_remainder)
        frontlink(p, sz, idx, bck, fwd);  
    }
  }
}


#if __STD_C
Void_t* REALLOC(Void_t* oldmem, size_t bytes)
#else
Void_t* REALLOC(oldmem, bytes) Void_t* oldmem; size_t bytes;
#endif
{
  size_t    nb;               /* padded request size */

  mchunkptr oldp;             /* chunk corresponding to oldmem */
  size_t    oldsize;          /* its size */

  mchunkptr newp;             /* chunk to return */
  size_t    newsize;          /* its size */
  Void_t*   newmem;           /* corresponding user mem */

  mchunkptr next;             /* next contiguous chunk after oldp */
  size_t    nextsize;         /* its size */

  mchunkptr prev;             /* previous contiguous chunk before oldp */
  size_t    prevsize;         /* its size */

  mchunkptr remainder;        /* holds split off extra space from newp */
  size_t    remainder_size;   /* its size */

  mchunkptr bck;              /* misc temp for linking */
  mchunkptr fwd;              /* misc temp for linking */

#ifdef REALLOC_ZERO_BYTES_FREES
  if (bytes == 0) { FREE(oldmem); return 0; }
#endif


  /* realloc of null is supposed to be same as malloc */
  if (oldmem == 0) return MALLOC(bytes);

  newp    = oldp    = mem2chunk(oldmem);
  newsize = oldsize = chunksize(oldp);

  nb = request2size(bytes);

#if HAVE_MMAP
  if(chunk_is_mmapped(oldp)) {
      Void_t* newmem;

      if(oldsize >= nb) return oldmem; /* do nothing */
      /* Must alloc, copy, free. */
      newmem = malloc(bytes);
      if (newmem == 0) return 0; /* propagate failure */
      MALLOC_COPY(newmem, oldmem, oldsize - SIZE_SZ);
      munmap_chunk(oldp);
      return newmem;
  }
#endif

  check_inuse_chunk(oldp);

  if ((long)(oldsize) < (long)(nb))  
  {

    /* Make sure all chunks are consolidated */
    if (recycle_list != 0) process_recycle_list();     

    /* Try expanding forward */

    next = chunk_at_offset(oldp, oldsize);
    if (next == top || !inuse(next)) 
    {
      nextsize = chunksize(next);

      /* Forward into top only if a remainder */
      if (next == top)
      {
        if ((long)(nextsize + newsize) >= (long)(nb + MINSIZE))
        {
          newsize += nextsize;
          top = chunk_at_offset(oldp, nb);
          set_head(top, (newsize - nb) | PREV_INUSE);
          set_head_size(oldp, nb);
          return chunk2mem(oldp);
        }
      }

      /* Forward into next chunk */
      else if (((long)(nextsize + newsize) >= (long)(nb)))
      { 
        unlink(next, bck, fwd);
        newsize  += nextsize;
        goto split;
      }
    }
    else
    {
      next = 0;
      nextsize = 0;
    }

    /* Try shifting backwards. */

    if (!prev_inuse(oldp))
    {
      prev = prev_chunk(oldp);
      prevsize = chunksize(prev);

      /* try forward + backward first to save a later consolidation */

      if (next != 0)
      {
        /* into top */
        if (next == top)
        {
          if ((long)(nextsize + prevsize + newsize) >= (long)(nb + MINSIZE))
          {
            unlink(prev, bck, fwd);
            newp = prev;
            newsize += prevsize + nextsize;
            newmem = chunk2mem(newp);
            MALLOC_COPY(newmem, oldmem, oldsize - SIZE_SZ);
            top = chunk_at_offset(newp, nb);
            set_head(top, (newsize - nb) | PREV_INUSE);
            set_head_size(newp, nb);
            return chunk2mem(newp);
          }
        }

        /* into next chunk */
        else if (((long)(nextsize + prevsize + newsize) >= (long)(nb)))
        {
          unlink(next, bck, fwd);
          unlink(prev, bck, fwd);
          newp = prev;
          newsize += nextsize + prevsize;
          newmem = chunk2mem(newp);
          MALLOC_COPY(newmem, oldmem, oldsize - SIZE_SZ);
          goto split;
        }
      }
      
      /* backward only */
      if (prev != 0 && (long)(prevsize + newsize) >= (long)nb)  
      {
        unlink(prev, bck, fwd);
        newp = prev;
        newsize += prevsize;
        newmem = chunk2mem(newp);
        MALLOC_COPY(newmem, oldmem, oldsize - SIZE_SZ);
        goto split;
      }
    }

    /* Must allocate */

    newmem = MALLOC (bytes);

    if (newmem == 0)  /* propagate failure */
      return 0; 

    /* Avoid copy if newp is next chunk after oldp. */
    /* (This can only happen when new chunk is sbrk'ed.) */

    if ( (newp = mem2chunk(newmem)) == next_chunk(oldp)) 
    {
      newsize += chunksize(newp);
      newp = oldp;
      goto split;
    }

    /* Otherwise copy, free, and exit */
    MALLOC_COPY(newmem, oldmem, oldsize - SIZE_SZ);
    FREE(oldmem);
    return newmem;
  }

 split:  /* split off extra room in old or expanded chunk */

  if (newsize - nb >= MINSIZE) /* split off remainder */
  {
    remainder = chunk_at_offset(newp, nb);
    remainder_size = newsize - nb;
    set_head_size(newp, nb);
    set_head(remainder, remainder_size | PREV_INUSE);
    set_inuse_bit_at_offset(remainder, remainder_size);
    FREE(chunk2mem(remainder)); /* let free() deal with it */
  }
  else
  {
    set_head_size(newp, newsize);
    set_inuse_bit_at_offset(newp, newsize);
  }

  check_inuse_chunk(newp);
  return chunk2mem(newp);
}




/* Return a pointer to space with at least the alignment requested */
/* Alignment argument must be a power of two */

#if __STD_C
Void_t* MEMALIGN(size_t alignment, size_t bytes)
#else
Void_t* MEMALIGN(alignment, bytes) size_t alignment; size_t bytes;
#endif
{
  mchunkptr p;                /* chunk obtained from malloc */
  char*     brk;              /* alignment point within p */
  mchunkptr newp;             /* chunk to return */
  size_t    newsize;          /* its size */
  size_t    leadsize;         /* leading space befor alignment point */
  mchunkptr remainder;        /* spare room at end to split off */
  long      remainder_size;   /* its size */

  /* Use an alignment that both we and the user can live with: */

  size_t align = (alignment > MINSIZE) ? alignment : MINSIZE;

  /* Call malloc with worst case padding to hit alignment; */

  size_t nb  = request2size(bytes);
  char*  m   = (char*)(MALLOC(nb + align + MINSIZE));

  if (m == 0) return 0; /* propagate failure */

  p = mem2chunk(m);

  if ((((size_t)(m)) % align) == 0) /* aligned */
  {
#if HAVE_MMAP
    if(chunk_is_mmapped(p))
      return chunk2mem(p); /* nothing more to do */
#endif
  }
  else /* misaligned */
  {
    /* 
      Find an aligned spot inside chunk.
      Since we need to give back leading space in a chunk of at 
      least MINSIZE, if the first calculation places us at
      a spot with less than MINSIZE leader, we can move to the
      next aligned spot -- we've allocated enough total room so that
      this is always possible.
    */

    brk = (char*) ( (((size_t)(m + align - 1)) & -align) - SIZE_SZ );
    if ((long)(brk - (char*)(p)) < MINSIZE) brk = brk + align;

    newp = (mchunkptr)brk;
    leadsize = brk - (char*)(p);
    newsize = chunksize(p) - leadsize;

#if HAVE_MMAP
    if(chunk_is_mmapped(p)) {
      *((size_t *)newp - 1) = *((size_t *)p - 1) + leadsize;
      set_head(newp, newsize|IS_MMAPPED);
      return chunk2mem(newp);
    }
#endif

    /* give back leader, use the rest */

    set_head(newp, newsize | PREV_INUSE);
    set_inuse_bit_at_offset(newp, newsize);
    set_head_size(p, leadsize);
    FREE(chunk2mem(p));
    p = newp;
  }

  /* Also give back spare room at the end */

  remainder_size = chunksize(p) - nb;

  if (remainder_size >= (long)MINSIZE)
  {
    remainder = chunk_at_offset(p, nb);
    set_head(remainder, remainder_size | PREV_INUSE);
    set_head_size(p, nb);
    FREE(chunk2mem(remainder));
  }

  check_inuse_chunk(p);
  return chunk2mem(p);

}



/* Derivatives */

#if __STD_C
Void_t* VALLOC(size_t bytes)
#else
Void_t* VALLOC(bytes) size_t bytes;
#endif
{
  return MEMALIGN (malloc_getpagesize, bytes);
}


#if __STD_C
Void_t* CALLOC(size_t n, size_t elem_size)
#else
Void_t* CALLOC(n, elem_size) size_t n; size_t elem_size;
#endif
{
  mchunkptr p;
  size_t csz;

  size_t sz = n * elem_size;
  Void_t* mem = MALLOC (sz);

  if (mem == 0) 
    return 0;
  else
  {
    p = mem2chunk(mem);
    csz = chunksize(p);
    MALLOC_ZERO(mem, csz - SIZE_SZ);
    return mem;
  }
}

#if !defined(INTERNAL_LINUX_C_LIB) || !defined(__ELF__)
#if __STD_C
void cfree(Void_t *mem)
#else
void cfree(mem) Void_t *mem;
#endif
{
  free(mem);
}
#endif



/* Non-standard routines */

/* If possible, release memory back to the system. Return 1 if successful */
/* (Can only release top-most memory, in page-size units.)  */

#if __STD_C
int malloc_trim(size_t pad)
#else
int malloc_trim(pad) size_t pad;
#endif
{
  long  top_size;        /* Amount of top-most memory */
  long  extra;           /* Amount to release */
  char* current_brk;     /* address returned by pre-check sbrk call */
  char* new_brk;         /* address returned by negative sbrk call */

  unsigned long pagesz = malloc_getpagesize;

  /* Make sure all chunks are consolidated */
  if (recycle_list != 0) process_recycle_list();     

  top_size = chunksize(top);
  extra = ((top_size - pad - MINSIZE + (pagesz-1)) / pagesz - 1) * pagesz;


  if (extra < (long)pagesz)  /* Not enough memory to release */
    return 0;

  else
  {
    /* Test to make sure no one else called sbrk */
    current_brk = (char*)(MORECORE (0));
    if (current_brk != (char*)(top) + top_size)
      return 0;     /* Apparently we don't own memory; must fail */

    else
    {
      new_brk = (char*)(MORECORE (-extra));
      
      if (new_brk == (char*)(MORECORE_FAILURE)) /* sbrk failed? */
      {
        /* Try to figure out what we have */
        current_brk = (char*)(MORECORE (0));
        top_size = current_brk - (char*)top;
        if (top_size >= (long)MINSIZE) /* if not, we are very very dead! */
        {
          sbrked_mem = current_brk - sbrk_base;
          set_head(top, top_size | PREV_INUSE);
        }
        check_chunk(top);
        return 0; 
      }

      else
      {
        /* Success. Adjust top accordingly. */
        set_head(top, (top_size - extra) | PREV_INUSE);
        sbrked_mem -= extra;
        check_chunk(top);
        return 1;
      }
    }
  }
}



#if __STD_C
size_t malloc_usable_size(Void_t* mem)
#else
size_t malloc_usable_size(mem) Void_t* mem;
#endif
{
  mchunkptr p;
  if (mem == 0)
    return 0;
  else
  {
    p = mem2chunk(mem);
    if(!chunk_is_mmapped(p))
    {
      if (!inuse(p)) return 0;
      check_inuse_chunk(p);
    }
    return chunksize(p) - SIZE_SZ;
  }
}



/* Utility to update mallinfo for malloc_stats and mallonfo() */

static void update_mallinfo() 
{
  int i;
  mbinptr b;
  mchunkptr p;
#if DEBUG
  mchunkptr q;
#endif

  int navail = 0;
  size_t avail = 0;
  size_t smavail = 0;
  int nsmavail = 0;

  avail = chunksize(top);
  current_mallinfo.keepcost = avail;

  if ((long)(chunksize(top)) >= (long)MINSIZE)
    navail++;

  for (i = 1; i < NAV; ++i)
  {
    b = bin_at(i);
    for (p = last(b); p != b; p = p->bk) 
    {
#if DEBUG
      check_free_chunk(p);
      for (q = next_chunk(p); 
           q != top && inuse(q) && (long)(chunksize(q)) >= (long)MINSIZE; 
           q = next_chunk(q))
        check_inuse_chunk(q);
#endif
      avail += chunksize(p);
      navail++;
    }
  }

  for (p = recycle_list; p != 0; p = p->fd)
  {
#if DEBUG
    check_inuse_chunk(p);
#endif
    smavail += chunksize(p);
    nsmavail++;
  }

  current_mallinfo.ordblks = navail;
  current_mallinfo.uordblks = sbrked_mem - avail - smavail;
  current_mallinfo.fordblks = avail;
  current_mallinfo.smblks = nsmavail;
  current_mallinfo.fsmblks = smavail;

}



void malloc_stats()
{
  update_mallinfo();
  fprintf(stderr, "maximum bytes = %10u\n", (unsigned int)max_sbrked_mem);
  fprintf(stderr, "current bytes = %10u\n", (unsigned int)sbrked_mem);
  fprintf(stderr, "in use  bytes = %10u\n", (unsigned int)(current_mallinfo.uordblks));
#if HAVE_MMAP
  fprintf(stderr, "mmaped chunks = %10u\n", n_mmaps);
#endif
}

struct mallinfo MALLINFO()
{
  update_mallinfo();
  return current_mallinfo;
}




#if __STD_C
int MALLOPT(int param_number, int value)
#else
int MALLOPT(param_number, value) int param_number; int value;
#endif
{
  switch(param_number) 
  {
    case M_MXFAST:
      max_recycle_size = value; return 1;
    case M_TRIM_THRESHOLD:
      trim_threshold = value; return 1; 
    case M_TOP_PAD:
      top_pad = value; return 1; 
#if HAVE_MMAP
    case M_MMAP_THRESHOLD:
      mmap_threshold = value; return 1;
    case M_MMAP_MAX:
      n_mmaps_max = value; return 1;
#endif

    default:
      return 0;
  }
}



