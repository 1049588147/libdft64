/*-
 * Copyright (c) 2010, Columbia University
 * All rights reserved.
 *
 * This software was developed by Vasileios P. Kemerlis <vpk@cs.columbia.edu>
 * at Columbia University, New York, NY, USA, in June 2010.
 *
 * Georgios Portokalidis <porto@cs.columbia.edu> contributed to the
 * optimized implementation of tagmap_setn() and tagmap_clrn()
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Columbia University nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "tagmap.h"
#include "branch_pred.h"
#include "debug.h"
#include "libdft_api.h"
#include "pin.H"
#include <cstdio>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libdft_api.h"

static unsigned int stdin_read_off = 0;
tag_dir_t tag_dir;
extern thread_ctx_t *threads_ctx;

inline void tag_dir_setb(tag_dir_t &dir, ADDRINT addr, tag_t const &tag) {
  if (addr > 0x7fffffffffff) {
    return;
  }
  // LOG("Setting tag "+hexstr(addr)+"\n");
  if (dir.table[VIRT2PAGETABLE(addr)] == NULL) {
    //  LOG("No tag table for "+hexstr(addr)+" allocating new table\n");
#ifndef _WIN32
    tag_table_t *new_table = new (std::nothrow) tag_table_t();
#else // _WIN32
    tag_table_t *new_table = new tag_table_t();
#endif
    if (new_table == NULL) {
      LOG("Failed to allocate tag table!\n");
      libdft_die();
    }
    dir.table[VIRT2PAGETABLE(addr)] = new_table;
  }

  tag_table_t *table = dir.table[VIRT2PAGETABLE(addr)];
  if ((*table).page[VIRT2PAGE(addr)] == NULL) {
    //    LOG("No tag page for "+hexstr(addr)+" allocating new page\n");
#ifndef _WIN32
    tag_page_t *new_page = new (std::nothrow) tag_page_t();
#else // _WIN32
    tag_page_t *new_page = new tag_page_t();
#endif
    if (new_page == NULL) {
      LOG("Failed to allocate tag page!\n");
      libdft_die();
    }
    std::fill(new_page->tag, new_page->tag + PAGE_SIZE,
              tag_traits<tag_t>::cleared_val);
    (*table).page[VIRT2PAGE(addr)] = new_page;
  }

  tag_page_t *page = (*table).page[VIRT2PAGE(addr)];
  (*page).tag[VIRT2OFFSET(addr)] = tag;
  /*
  if (!tag_is_empty(tag)) {
    LOGD("[!]Writing tag for %p \n", (void *)addr);
  }
  */
}

inline tag_t const *tag_dir_getb_as_ptr(tag_dir_t const &dir, ADDRINT addr) {
 
  if (addr > 0x7fffffffffff) {
  printf("addr > 0x7fffffffffff");
  fflush(stdout); 
    return NULL;
  }
  if (dir.table[VIRT2PAGETABLE(addr)]) {
      
    tag_table_t *table = dir.table[VIRT2PAGETABLE(addr)];
    if ((*table).page[VIRT2PAGE(addr)]) {
      
      tag_page_t *page = (*table).page[VIRT2PAGE(addr)];
      if (page != NULL){
          
      return &(*page).tag[VIRT2OFFSET(addr)];
      }
        
    }
  }
  return &tag_traits<tag_t>::cleared_val;
}

// PIN_FAST_ANALYSIS_CALL
void tagmap_setb(ADDRINT addr, tag_t const &tag) {
  tag_dir_setb(tag_dir, addr, tag);
}

void tagmap_setb_reg(THREADID tid, unsigned int reg_idx, unsigned int off,
                     tag_t const &tag) {
  threads_ctx[tid].vcpu.gpr[reg_idx][off] = tag;
}

tag_t tagmap_getb(ADDRINT addr) { return *tag_dir_getb_as_ptr(tag_dir, addr); }

tag_t tagmap_getb_reg(THREADID tid, unsigned int reg_idx, unsigned int off) {
  return threads_ctx[tid].vcpu.gpr[reg_idx][off];
}

tag_t tagmap_getw(ADDRINT addr) { return tagmap_getn(addr, sizeof(uint16_t)); }

tag_t tagmap_getl(ADDRINT addr) { return tagmap_getn(addr, sizeof(uint32_t)); }

void PIN_FAST_ANALYSIS_CALL tagmap_clrb(ADDRINT addr) {
  tagmap_setb(addr, tag_traits<tag_t>::cleared_val);
}

void PIN_FAST_ANALYSIS_CALL tagmap_clrn(ADDRINT addr, UINT32 n) {
  ADDRINT i;
  for (i = addr; i < addr + n; i++) {
    tagmap_clrb(i);
  }
}

void PIN_FAST_ANALYSIS_CALL tagmap_setn(ADDRINT addr, UINT32 n, tag_t const &tag) {
  ADDRINT i;
  for (i = addr; i < addr + n; i++) {
    tagmap_setb(i, tag);
  }
}


void PIN_FAST_ANALYSIS_CALL tagmap_setd(void *ctx,ADDRINT buf, UINT32 nr)
{

   const int fd = ((syscall_ctx_t*)ctx)->arg[SYSCALL_ARG0];
    unsigned int read_off = 0;
    if (fd == STDIN_FILENO) {
      // maintain it by ourself
      read_off = stdin_read_off;
      stdin_read_off += nr;
    } else {
      // low-level POSIX file descriptor I/O.
      read_off = lseek(fd, 0, SEEK_CUR);
      read_off -= nr; // post
    }

    for (unsigned int i = 0; i < nr; i++) {
      tag_t t = tag_alloc<tag_t>(read_off + i);

      FILE *f;
      f=fopen("/tmp/libdft-dta.log","a");
      fprintf(f,"标记为:%d\n",t);
      fclose(f);

      tagmap_setb(buf + i, t);
      // LOGD("[read] %d, lb: %d,  %s\n", i, t, tag_sprint(t).c_str());
    
  }   
   
}


tag_t tagmap_getn(ADDRINT addr, unsigned int n) {
  tag_t ts = tag_traits<tag_t>::cleared_val;

  for (size_t i = 0; i < n; i++) {
    const tag_t t = tagmap_getb(addr + i);
    if (tag_is_empty(t)){
      printf("tagmap_getb(addr + i) is empty!");
  fflush(stdout); 
  continue;
    }
    else{
      printf(" tagmap_getb_taint: %s\n", tag_sprint(t).c_str());
  fflush(stdout);
    } 
    // LOGD("[tagmap_getn] %lu, ts: %d, %s\n", i, ts, tag_sprint(t).c_str());
    ts = tag_combine(ts, t);
    // LOGD("t: %d, ts:%d\n", t, ts);
  }
  return ts;
}

tag_t tagmap_getn_reg(THREADID tid, unsigned int reg_idx, unsigned int n) {
  tag_t ts = tag_traits<tag_t>::cleared_val;
  for (size_t i = 0; i < n; i++) {
    const tag_t t = tagmap_getb_reg(tid, reg_idx, i);
    if (tag_is_empty(t))
      continue;
    // LOGD("[tagmap_getn] %lu, ts: %d, %s\n", i, ts, tag_sprint(t).c_str());
    ts = tag_combine(ts, t);
    // LOGD("t: %d, ts:%d\n", t, ts);
  }
  return ts;
}
