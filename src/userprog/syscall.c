#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/vaddr.h"
#include "lib/kernel/stdio.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "lib/kernel/bitmap.h"
#include "devices/input.h"


static void syscall_handler (struct intr_frame *);

#define FILE_ID_OFFSET 2

#define CHECK_POINTER(P)                                        \
  do {                                                          \
    if ((uint32_t)(P) >= (uint32_t)PHYS_BASE || !pagedir_get_page (thread_current ()->pagedir, (P))) \
    {                                                           \
      thread_exit (-1);                                         \
      return;                                                   \
    }                                                           \
  } while (0)

static bool
check_string (char *str)
{
  if (str == NULL)
    return false;

  while (*str != '\0')
  {
    str++;
    if ((uint32_t)str >= (uint32_t)PHYS_BASE)
      return false;
  }

  return true;
}

#define CHECK_STRING(S)                           \
  do {                                            \
    if (!check_string (S))                        \
    {                                             \
      thread_exit (-1);                           \
      return;                                     \
    }                                             \
  } while (0)


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_halt (void)
{
  power_off ();
}

static void
syscall_exit (struct intr_frame *f)
{
  int32_t *esp;

  int exit_code;
  
  esp = f->esp;
  esp++;
  CHECK_POINTER (esp);
  
  exit_code = (int)*esp;

  thread_exit (exit_code);
}

static void
syscall_exec (struct intr_frame *f)
{
  int32_t *esp;
  char *cmd_line;
  tid_t tid;

  esp = f->esp;
  esp++;
  CHECK_POINTER (esp);

  cmd_line = (char *)*esp;
  CHECK_POINTER (cmd_line);
  CHECK_STRING (cmd_line);

  tid = process_execute (cmd_line);
  if (tid == TID_ERROR)
    f->eax = -1;
  else
    f->eax = tid;
}

static void
syscall_wait (struct intr_frame *f)
{
  int32_t *esp;
  tid_t tid;

  esp = f->esp;
  esp++;
  CHECK_POINTER (esp);

  tid = (tid_t)*esp;

  f->eax = process_wait (tid);
}

static void
syscall_read (struct intr_frame *f)
{
  int32_t *esp;
  int fd;
  void *buf;
  unsigned size;

  esp = f->esp;
  esp++;
  CHECK_POINTER (esp + 2);

  fd = (int)*esp++;

  buf = (void *)*esp++;
  size = (unsigned)*esp;

  /* Check so that buf lies in userspace */
  CHECK_POINTER (buf);
  CHECK_POINTER (buf + size);

  if (fd == STDIN_FILENO)
    {
      uint8_t *cbuf = (uint8_t *)buf;
      unsigned i;

      for (i = 0; i < size; i++)
	  cbuf[i] = input_getc ();
      
      f->eax = size;
    }
  else if (fd >= FILE_ID_OFFSET && fd < (FILE_ID_OFFSET + MAX_FILES))
    {
      struct thread *cur = thread_current ();
      int id = fd - FILE_ID_OFFSET;

      if (!bitmap_test (cur->files_bitmap, id))
	{
	  f->eax = -1;
	  return;
	}
      
      f->eax = file_read (cur->files[id], buf, size);
    }
  else
    f->eax = -1;
}

static void
syscall_write (struct intr_frame *f)
{
  int32_t *esp;
  int fd;
  void *buf;
  unsigned size;

  esp = f->esp;
  esp++;
  CHECK_POINTER (esp + 2);

  fd = (int)*esp++;

  buf = (void *)*esp++;
  size = (unsigned)*esp;

  /* Check so that buf lies in userspace */
  CHECK_POINTER (buf);
  CHECK_POINTER (buf + size);
  
  if (fd == STDOUT_FILENO)
    {
      putbuf (buf, size);
      f->eax = size;
    }
  else if (fd >= FILE_ID_OFFSET && fd < (FILE_ID_OFFSET + MAX_FILES))
    {
      struct thread *cur = thread_current ();
      int id = fd - FILE_ID_OFFSET;

      if (!bitmap_test (cur->files_bitmap, id))
	{
	  f->eax = -1;
	  return;
	}
      
      f->eax = file_write (cur->files[id], buf, size);
    }
  else
    f->eax = -1;
}

static void
syscall_create (struct intr_frame *f)
{
  int32_t *esp;
  char *name;
  unsigned size;

  esp = f->esp;
  esp++;
  CHECK_POINTER (esp + 1);

  name = (char *)*esp++;
  CHECK_POINTER (name);
  CHECK_STRING (name);
  size = (unsigned)*esp;

  f->eax = filesys_create (name, size);
}

static void
syscall_open (struct intr_frame *f)
{
  int32_t *esp;
  char *name;

  struct thread *cur = thread_current ();
  unsigned id;
  struct file *file;

  esp = f->esp;
  esp++;
  CHECK_POINTER (esp);

  name = (char *)*esp;
  CHECK_POINTER (name);
  CHECK_STRING (name);

  id = bitmap_scan_and_flip (cur->files_bitmap, 0, 1, 0);
  if (id == BITMAP_ERROR)
    {
      f->eax = -1;
      return;
    }

  file = filesys_open (name);
  if (file == NULL)
    {
      bitmap_reset (cur->files_bitmap, id);
      f->eax = -1;
      return;
    }
  
  cur->files[id] = file;
  
  f->eax = id + FILE_ID_OFFSET;
}

static void
syscall_close (struct intr_frame *f)
{
  int32_t *esp;
  int fd;

  esp = f->esp;
  esp++;
  CHECK_POINTER (esp);
  
  fd = (int)*esp;

  if (fd >= FILE_ID_OFFSET && fd < (FILE_ID_OFFSET + MAX_FILES))
    {
      struct thread *cur = thread_current ();
      unsigned id = fd - FILE_ID_OFFSET;

      if (bitmap_test (cur->files_bitmap, id))
	{
	  bitmap_reset (cur->files_bitmap, id);
	  file_close (cur->files[id]);
	}
    }
}

static void
syscall_handler (struct intr_frame *f) 
{
  int32_t *esp;
  int syscall_nr;

  esp = f->esp;
  CHECK_POINTER (esp);
  syscall_nr = *esp;

  switch (syscall_nr)
  {
    case SYS_HALT:
      syscall_halt ();
      break;
    case SYS_EXIT:
      syscall_exit (f);
      break;
    case SYS_WAIT:
      syscall_wait (f);
      break;
    case SYS_EXEC:
      syscall_exec (f);
      break;
    case SYS_CREATE:
      syscall_create (f);
      break;
    case SYS_OPEN:
      syscall_open (f);
      break;
    case SYS_READ:
      syscall_read (f);
      break;
    case SYS_WRITE:
      syscall_write (f);
      break;
    case SYS_CLOSE:
      syscall_close (f);
      break;
    default:
      printf ("Syscall nr: %d is not implemented!", syscall_nr);
      thread_exit (-1);
    }
}
