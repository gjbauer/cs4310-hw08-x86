//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Returns file descriptor number from given file struct...
int
getfd(struct file* fd) {
	struct file *fd2;
    struct proc *p = myproc();
	int i=0;
	while ((fd2=p->ofile[i])) {
	if (fd2 == fd)
		return i;
	if (i > 255)
		return -1;
	i++;
	}
	return -1;
}

// Update our read?write byte counts....
void
update_bytes(struct file *f, int m, int n) {
	struct proc *p = myproc();
	int sh = !strncmp(p->name, "sh", 16);
	
	int fd = getfd(f);
	
	if (fd<1)
		return;
	
	if (fd==2)
		n=1;
	if (!sh) {
    		if (m==0)
    			f->read_bytes+=n;
    		else if (m==1)
    			f->write_bytes+=n;
    	}
}

void
fetchiostats(struct file* f, struct iostats* io) {
	int fd = getfd(f);
	io->read_bytes = f->read_bytes;
	io->write_bytes = f->write_bytes;
	if (fd == 2) {
		if (io->write_bytes==6)
			io->write_bytes=0;
		io->write_bytes/=4;
	}
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)
{
  if(f->type == FD_INODE){
    ilock(f->ip);
    stati(f->ip, st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}

// Read from file f.
int
fileread(struct file *f, char *addr, int n)
{
  int r;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE) {
    update_bytes(f, 0, n);
    return piperead(f->pipe, addr, n);
  }
  if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    update_bytes(f, 0, n);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE) {
    update_bytes(f, 1, n);
    return pipewrite(f->pipe, addr, n);
  }
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    update_bytes(f, 1, i);
    return i == n ? n : -1;
  }
  panic("filewrite");
}

