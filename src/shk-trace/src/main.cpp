/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * "Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 1.0 (the 'License').  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License."
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <libc.h>
#include <errno.h>

#include <sys/mman.h>

#include "kdebug.h"
#include "event_info.h"
#include "syscall_constants.h"
#include "syscall_tables.h"
#include "sysctl_helpers.h"

#define PATHLENGTH (NUMPARMS*sizeof(uintptr_t))

extern "C" int reexec_to_match_kernel();

namespace shk {

struct threadmap_entry {
  unsigned int tm_setsize = 0; // This is a bit count
  unsigned long *tm_setptr = nullptr;  // File descriptor bitmap
  char tm_command[MAXCOMLEN + 1];
};

std::unordered_map<uintptr_t, threadmap_entry> threadmap;
std::unordered_map<uint64_t, std::string> vn_name_map;
event_info_map ei_map;


int need_new_map = 1;

char *arguments = 0;
int argmax = 0;


#define USLEEP_MIN 1
#define USLEEP_BEHIND 2
#define USLEEP_MAX 32

void    format_print(event_info *, uintptr_t, int, uintptr_t, uintptr_t, uintptr_t, uintptr_t, const bsd_syscall &, const char *);
void    enter_event_now(uintptr_t, int, kd_buf *, const char *);
void    enter_event(uintptr_t thread, int type, kd_buf *kd, const char *name);
void    enter_illegal_event(uintptr_t thread, int type);
void    exit_event(uintptr_t, int, uintptr_t, uintptr_t, uintptr_t, uintptr_t, const bsd_syscall &);

void    init_arguments_buffer();
int     get_real_command_name(int, char *, int);

void    read_command_map(const kbufinfo_t &bufinfo);
void    create_map_entry(uintptr_t, int, char *);

void    set_remove();

static const auto bsd_syscalls = make_bsd_syscall_table();

#define EVENT_BASE 60000
#define DBG_FUNC_MASK 0xfffffffc

int trace_enabled = 0;

void set_enable(bool enabled);

/*
 *  signal handlers
 */

void leave(int sig) {  // Signal handler for exiting under normal conditions
  fflush(0);

  set_enable(false);
  set_remove();

  exit(0);
}

void set_enable(bool enabled) {
  enable_kdebug(enabled);
  trace_enabled = enabled;
}

void set_remove()  {
  try {
    kdebug_teardown();
  } catch (std::runtime_error &error) {
    if (trace_enabled) {
      set_enable(false);
    }

    exit(1);
  }
}

int sample_sc(std::vector<kd_buf> &event_buffer) {
  kbufinfo_t bufinfo = get_kdebug_bufinfo();

  if (need_new_map) {
    read_command_map(bufinfo);
    need_new_map = 0;
  }

  size_t count = kdebug_read_buf(event_buffer.data(), bufinfo.nkdbufs);

  int usleep_ms = USLEEP_MIN;
  if (count > (event_buffer.size() / 8)) {
    if (usleep_ms > USLEEP_BEHIND) {
      usleep_ms = USLEEP_BEHIND;
    } else if (usleep_ms > USLEEP_MIN) {
      usleep_ms /= 2;
    }
  } else if (count < (event_buffer.size() / 16)) {
    if (usleep_ms < USLEEP_MAX) {
      usleep_ms *= 2;
    }
  }

  if (bufinfo.flags & KDBG_WRAPPED) {
    throw std::runtime_error("Buffer overrun! Event data has been lost");
  }
  kd_buf *kd = event_buffer.data();

  for (int i = 0; i < count; i++) {
    uintptr_t thread = kd[i].arg5;
    uint32_t debugid = kd[i].debugid;
    int type = kd[i].debugid & DBG_FUNC_MASK;

    switch (type) {
    case TRACE_DATA_NEWTHREAD:
      if (kd[i].arg1) {
        event_info *ei = &ei_map.add_event(thread, TRACE_DATA_NEWTHREAD)->second;
        ei->child_thread = kd[i].arg1;
        ei->pid = kd[i].arg2;
        // TODO(peck): Removeme
        // printf("newthread PID %d (thread = %d, child_thread = %d)\n", (int)ei->pid, (int)thread, ei->child_thread);
      }
      continue;

    case TRACE_STRING_NEWTHREAD:
      {
        auto ei_it = ei_map.find(thread, TRACE_DATA_NEWTHREAD);
        if (ei_it == ei_map.end()) {
          continue;
        }
        event_info *ei = &ei_it->second;

        create_map_entry(ei->child_thread, ei->pid, (char *)&kd[i].arg1);

        ei_map.erase(ei_it);
        continue;
      }
  
    case TRACE_DATA_EXEC:
      {
        event_info *ei = &ei_map.add_event(thread, TRACE_DATA_EXEC)->second;
        ei->pid = kd[i].arg1;
        continue;
      }

    case TRACE_STRING_EXEC:
      {
        auto ei_it = ei_map.find(thread, BSC_execve);
        if (ei_it != ei_map.end()) {
          if (ei_it->second.lookups[0].pathname[0]) {
            exit_event(thread, BSC_execve, 0, 0, 0, 0, bsd_syscalls[BSC_INDEX(BSC_execve)]);
          }
        } else if ((ei_it = ei_map.find(thread, BSC_posix_spawn)) != ei_map.end()) {
          if (ei_it->second.lookups[0].pathname[0]) {
            exit_event(thread, BSC_posix_spawn, 0, 0, 0, 0, bsd_syscalls[BSC_INDEX(BSC_execve)]);
          }
        }
        ei_it = ei_map.find(thread, TRACE_DATA_EXEC);

        create_map_entry(thread, ei_it->second.pid, (char *)&kd[i].arg1);

        ei_map.erase(ei_it);
        continue;
      }

    case BSC_thread_terminate:
      threadmap.erase(thread);
      continue;

    case BSC_exit:
      continue;

    case proc_exit:
      kd[i].arg1 = kd[i].arg2 >> 8;
      type = BSC_exit;
      break;

    case BSC_mmap:
      if (kd[i].arg4 & MAP_ANON) {
        continue;
      }
      break;

    case VFS_ALIAS_VP:
      {
        auto name_it = vn_name_map.find(kd[i].arg1);
        if (name_it != vn_name_map.end()) {
          vn_name_map[kd[i].arg2] = name_it->second;
        } else {
          // TODO(peck): Can this happen?
          vn_name_map.erase(kd[i].arg2);
        }
        continue;
      }

    case VFS_LOOKUP:
      {
        auto ei_it = ei_map.find_last(thread);
        if (ei_it == ei_map.end()) {
          continue;
        }
        event_info *ei = &ei_it->second;

        uintptr_t *sargptr;
        if (debugid & DBG_FUNC_START) {

          if (ei->type == HFS_update) {
            ei->pn_work_index = (MAX_PATHNAMES - 1);
          } else {
            if (ei->pn_scall_index < MAX_SCALL_PATHNAMES) {
              ei->pn_work_index = ei->pn_scall_index;
            } else {
              continue;
            }
          }
          sargptr = &ei->lookups[ei->pn_work_index].pathname[0];

          ei->vnodeid = kd[i].arg1;

          *sargptr++ = kd[i].arg2;
          *sargptr++ = kd[i].arg3;
          *sargptr++ = kd[i].arg4;
          *sargptr = 0;

          ei->pathptr = sargptr;
        } else {
          sargptr = ei->pathptr;

          // We don't want to overrun our pathname buffer if the
          // kernel sends us more VFS_LOOKUP entries than we can
          // handle and we only handle 2 pathname lookups for
          // a given system call.
          if (sargptr == 0) {
            continue;
          }

          if ((uintptr_t)sargptr < (uintptr_t)&ei->lookups[ei->pn_work_index].pathname[NUMPARMS]) {

            *sargptr++ = kd[i].arg1;
            *sargptr++ = kd[i].arg2;
            *sargptr++ = kd[i].arg3;
            *sargptr++ = kd[i].arg4;
            *sargptr = 0;
          }
        }
        if (debugid & DBG_FUNC_END) {

          vn_name_map[ei->vnodeid] =
              reinterpret_cast<const char *>(&ei->lookups[ei->pn_work_index].pathname[0]);

          if (ei->pn_work_index == ei->pn_scall_index) {

            ei->pn_scall_index++;

            if (ei->pn_scall_index < MAX_SCALL_PATHNAMES) {
              ei->pathptr = &ei->lookups[ei->pn_scall_index].pathname[0];
            } else {
              ei->pathptr = 0;
            }
          }
        } else {
          ei->pathptr = sargptr;
        }

        continue;
      }
    }

    if (debugid & DBG_FUNC_START) {
      if ((type & CLASS_MASK) == FILEMGR_BASE) {
        enter_illegal_event(thread, type);
      } else {
        enter_event(thread, type, &kd[i], nullptr);
      }
      continue;
    }

    switch (type) {
    case Throttled:
      {
        static bsd_syscall syscall;
        syscall.name = "  THROTTLED";
        exit_event(thread, type, 0, 0, 0, 0, syscall);
        continue;
      }

    case HFS_update:
      {
        static bsd_syscall syscall;
        syscall.name = "  HFS_update";
        syscall.format = Fmt::HFS_update;
        exit_event(thread, type, kd[i].arg1, kd[i].arg2, 0, 0, syscall);
        continue;
      }

    case SPEC_unmap_info:
      {
        // TODO(peck): Is this ignored code?
        static bsd_syscall syscall;
        syscall.name = "  TrimExtent";
        format_print(NULL, thread, type, kd[i].arg1, kd[i].arg2, kd[i].arg3, 0, syscall, nullptr);
        continue;
      }

    case MACH_pageout:
    case MACH_vmfault:
      {
        auto ei_it = ei_map.find(thread, type);
        if (ei_it != ei_map.end()) {
          ei_map.erase(ei_it);
        }
        continue;
      }

    case MSC_map_fd:
      {
        // TODO(peck): Is this ignored code?
        static bsd_syscall syscall;
        syscall.name = "map_fd";
        exit_event(thread, type, kd[i].arg1, kd[i].arg2, 0, 0, syscall);
        continue;
      }
    }

    if ((type & CSC_MASK) == BSC_BASE) {
      int index = BSC_INDEX(type);
      if (index >= bsd_syscalls.size()) {
        continue;
      }

      if (bsd_syscalls[index].name) {
        exit_event(thread, type, kd[i].arg1, kd[i].arg2, kd[i].arg3, kd[i].arg4, bsd_syscalls[index]);

        if (type == BSC_exit) {
          threadmap.erase(thread);
        }
      }
    }
  }
  fflush(0);

  return usleep_ms;
}


void enter_event_now(uintptr_t thread, int type, kd_buf *kd, const char *name) {
  auto ei_it = ei_map.add_event(thread, type);
  auto *ei = &ei_it->second;

  ei->arg1 = kd->arg1;
  ei->arg2 = kd->arg2;
  ei->arg3 = kd->arg3;
  ei->arg4 = kd->arg4;
}


void enter_event(uintptr_t thread, int type, kd_buf *kd, const char *name) {
  switch (type) {

  case MSC_map_fd:
  case Throttled:
  case HFS_update:
    enter_event_now(thread, type, kd, name);
    return;

  }
  if ((type & CSC_MASK) == BSC_BASE) {
    int index = BSC_INDEX(type);
    if (index >= bsd_syscalls.size()) {
      return;
    }

    if (bsd_syscalls[index].name) {
      enter_event_now(thread, type, kd, name);
    }
    return;
  }
}


void enter_illegal_event(uintptr_t thread, int type) {
  // TODO(peck): Only exist if the thread is one that is traced
  fprintf(stderr, "Encountered illegal syscall (perhaps a Carbon File Manager)\n");
  exit(1);
}


void exit_event(
    uintptr_t thread,
    int type,
    uintptr_t arg1,
    uintptr_t arg2,
    uintptr_t arg3,
    uintptr_t arg4,
    const bsd_syscall &syscall) {
  auto ei_it = ei_map.find(thread, type);
  if (ei_it == ei_map.end()) {
    return;
  }

  auto *ei = &ei_it->second;
  format_print(ei, thread, type, arg1, arg2, arg3, arg4, syscall, (char *)&ei->lookups[0].pathname[0]);
  ei_map.erase(ei_it);
}


void format_print(
    event_info *ei,
    uintptr_t thread,
    int type,
    uintptr_t arg1,
    uintptr_t arg2,
    uintptr_t arg3,
    uintptr_t arg4,
    const bsd_syscall &syscall,
    const char *pathname /* nullable */) {
  char buf[(PATHLENGTH + 80) + 64];

  threadmap_entry *tme;

  auto tme_it = threadmap.find(thread);
  const char *command_name = tme_it == threadmap.end() ?
      "" : tme_it->second.tm_command;

  printf("  %-17.17s", syscall.name);

  off_t offset_reassembled = 0LL;

  switch (syscall.format) {
  case Fmt::IGNORE:
    break;

  case Fmt::HFS_update:
  {
    char sbuf[7];
    int sflag = (int)arg2;

    memset(sbuf, '_', 6);
    sbuf[6] = '\0';

    if (sflag & 0x10) {
      sbuf[0] = 'F';
    }
    if (sflag & 0x08) {
      sbuf[1] = 'M';
    }
    if (sflag & 0x20) {
      sbuf[2] = 'D';
    }
    if (sflag & 0x04) {
      sbuf[3] = 'c';
    }
    if (sflag & 0x01) {
      sbuf[4] = 'a';
    }
    if (sflag & 0x02) {
      sbuf[5] = 'm';
    }

    printf("            (%s) ", sbuf);

    auto name_it = vn_name_map.find(arg1);
    pathname = name_it == vn_name_map.end() ? nullptr : name_it->second.c_str();

    break;
  }

  case Fmt::OPEN:
  {
    char mode[7];

    memset(mode, '_', 6);
    mode[6] = '\0';

    if (ei->arg2 & O_RDWR) {
      mode[0] = 'R';
      mode[1] = 'W';
    } else if (ei->arg2 & O_WRONLY) {
      mode[1] = 'W';
    } else {
      mode[0] = 'R';
    }

    if (ei->arg2 & O_CREAT) {
      mode[2] = 'C';
    }

    if (ei->arg2 & O_APPEND) {
      mode[3] = 'A';
    }

    if (ei->arg2 & O_TRUNC) {
      mode[4] = 'T';
    }

    if (ei->arg2 & O_EXCL) {
      mode[5] = 'E';
    }

    if (arg1) {
      printf("      [%3lu] (%s) ", arg1, mode);
    } else {
      printf(" F=%-3lu      (%s) ", arg2, mode);
    }

    break;
  }

  case Fmt::CREATE:
    printf("create");
    break;

  case Fmt::DELETE:
    printf("delete");
    break;

  case Fmt::READ_CONTENTS:
    printf("read_contents");
    break;

  case Fmt::WRITE_CONTENTS:
    printf("write_contents");
    break;

  case Fmt::READ_METADATA:
  case Fmt::FD_READ_METADATA:
    printf("read_metadata");
    break;

  case Fmt::WRITE_METADATA:
  case Fmt::FD_WRITE_METADATA:
    printf("write_metadata");
    break;

  case Fmt::CREATE_DIR:
    printf("create_dir");
    break;

  case Fmt::DELETE_DIR:
    printf("delete_dir");
    break;

  case Fmt::READ_DIR:
  case Fmt::FD_READ_DIR:
    printf("read_dir");
    break;

  case Fmt::EXCHANGE:
    printf("exchange");
    break;

  case Fmt::RENAME:
    printf("rename");
    break;

  case Fmt::ILLEGAL:
    printf("[[ILLEGAL]]");
    break;
  }

  if (pathname) {
    if (syscall.at == SyscallAt::YES) {
      int at = syscall.format == Fmt::RENAME ? ei->arg3 : ei->arg1;
      sprintf(&buf[0], " [%d]/%s ", at, pathname);
    } else {
      sprintf(&buf[0], " %s ", pathname);
    }
  }

  pathname = buf;

  printf("%s %s.%d\n", pathname, command_name, (int)thread);
}

// TODO(peck): We don't need to track command names really
void read_command_map(const kbufinfo_t &bufinfo) {
  kd_threadmap *mapptr = 0;

  threadmap.clear();

  int total_threads = bufinfo.nkdthreads;
  size_t size = bufinfo.nkdthreads * sizeof(kd_threadmap);

  if (size) {
    if ((mapptr = reinterpret_cast<kd_threadmap *>(malloc(size)))) {
      bzero (mapptr, size);
      // Now read the threadmap
      static int name[] = { CTL_KERN, KERN_KDEBUG, KERN_KDTHRMAP, 0, 0, 0 };
      if (sysctl(name, 3, mapptr, &size, NULL, 0) < 0) {
        // This is not fatal -- just means I cant map command strings
        free(mapptr);
        return;
      }
    }
  }
  for (int i = 0; i < total_threads; i++) {
    create_map_entry(mapptr[i].thread, mapptr[i].valid, &mapptr[i].command[0]);
  }

  free(mapptr);
}


void create_map_entry(uintptr_t thread, int pid, char *command) {
  auto &tme = threadmap[thread];

  strncpy(tme.tm_command, command, MAXCOMLEN);
  tme.tm_command[MAXCOMLEN] = '\0';

  if (pid != 0 && pid != 1) {
    if (!strncmp(command, "LaunchCFMA", 10)) {
      (void)get_real_command_name(pid, tme.tm_command, MAXCOMLEN);
    }
  }
}

// TODO(peck): We don't need to track command names really
void init_arguments_buffer() {
  size_t size = sizeof(argmax);

  static int name[] = { CTL_KERN, KERN_ARGMAX };
  if (sysctl(name, 2, &argmax, &size, NULL, 0) == -1) {
    return;
  }
  // Hack to avoid kernel bug.
  if (argmax > 8192) {
    argmax = 8192;
  }
  arguments = (char *)malloc(argmax);
}


// TODO(peck): We don't need to track command names really
int get_real_command_name(int pid, char *cbuf, int csize) {
  char *cp;
  char *command_beg, *command, *command_end;

  if (cbuf == NULL) {
    return 0;
  }

  if (arguments) {
    bzero(arguments, argmax);
  } else {
    return 0;
  }

  // A sysctl() is made to find out the full path that the command
  // was called with.
  static int name[] = { CTL_KERN, KERN_PROCARGS2, pid, 0 };
  if (sysctl(name, 3, arguments, (size_t *)&argmax, NULL, 0) < 0) {
    return 0;
  }

  // Skip the saved exec_path
  for (cp = arguments; cp < &arguments[argmax]; cp++) {
    if (*cp == '\0') {
      // End of exec_path reached
      break;
    }
  }
  if (cp == &arguments[argmax]) {
    return 0;
  }

  // Skip trailing '\0' characters
  for (; cp < &arguments[argmax]; cp++) {
    if (*cp != '\0') {
      // Beginning of first argument reached
      break;
    }
  }
  if (cp == &arguments[argmax]) {
    return 0;
  }

  command_beg = cp;
  // Make sure that the command is '\0'-terminated.  This protects
  // against malicious programs; under normal operation this never
  // ends up being a problem..
  for (; cp < &arguments[argmax]; cp++) {
    if (*cp == '\0') {
      // End of first argument reached
      break;
    }
  }
  if (cp == &arguments[argmax]) {
    return 0;
  }

  command_end = command = cp;

  // Get the basename of command
  for (command--; command >= command_beg; command--) {
    if (*command == '/') {
      command++;
      break;
    }
  }
  strncpy(cbuf, (char *)command, csize);
  cbuf[csize-1] = '\0';

  return 1;
}

int main(int argc, char *argv[]) {
  if (0 != reexec_to_match_kernel()) {
    fprintf(stderr, "Could not re-execute: %d\n", errno);
    exit(1);
  }

  if (geteuid() != 0) {
    fprintf(stderr, "This tool must be run as root\n");
    exit(1);
  }

  // Set up signal handlers
  signal(SIGINT, leave);
  signal(SIGQUIT, leave);
  signal(SIGPIPE, leave);

  struct sigaction osa;
  sigaction(SIGHUP, (struct sigaction *)NULL, &osa);

  if (osa.sa_handler == SIG_DFL) {
    signal(SIGHUP, leave);
  }
  signal(SIGTERM, leave);

  std::vector<kd_buf> event_buffer(EVENT_BASE * get_num_cpus());

  set_remove();
  set_kdebug_numbufs(event_buffer.size());
  kdebug_setup();

  set_kdebug_filter();
  set_enable(true);
  init_arguments_buffer();

  for (;;) {
    auto usleep_ms = sample_sc(event_buffer);
    usleep(1000 * usleep_ms);
  }
}

}  // namespace shk

int main(int argc, char *argv[]) {
  shk::main(argc, argv);
}
