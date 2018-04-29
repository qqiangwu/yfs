/*
 * receive request from fuse and call methods of yfs_client
 *
 * started life as low-level example in the fuse distribution.  we
 * have to use low-level interface in order to get i-numbers.  the
 * high-level interface only gives us complete paths.
 */

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <limits>
#include "lang/verify.h"
#include "yfs_client.h"
#include "log.h"

int myid;
std::unique_ptr<yfs::yfs_client> fs;

inline int id()
{
  return myid;
}

template <class Fn>
inline void run(fuse_req_t req, Fn&& fn) noexcept
try {
    fn();
} catch (yfs::No_entry_error& e) {
    YLOG_ERROR("error(no_entry: %s)", e.what());
    fuse_reply_err(req, ENOENT);
} catch (yfs::Already_exist_error& e) {
    YLOG_ERROR("error(exist: %s)", e.what());
    fuse_reply_err(req, EEXIST);
} catch (std::exception& e) {
    YLOG_ERROR("error(unknown: %s)", e.what());
    fuse_reply_err(req, EIO);
}

struct stat getattr(const yfs::inum_t inum)
{
    struct stat st {};
    st.st_ino = inum;

    if (fs->isfile(inum)) {
        auto attr = fs->getfile(inum);
        if (!attr) {
            throw yfs::No_entry_error{"no file"};
        }

        st.st_mode = S_IFREG | 0666;
        st.st_nlink = 1;
        st.st_atime = attr->atime;
        st.st_mtime = attr->mtime;
        st.st_ctime = attr->ctime;
        st.st_size  = attr->size;
    } else {
        auto attr = fs->getdir(inum);
        if (!attr) {
            throw yfs::No_entry_error{"no dir"};
        }

        st.st_mode = S_IFDIR | 0777;
        st.st_nlink = 2;
        st.st_atime = attr->atime;
        st.st_mtime = attr->mtime;
        st.st_ctime = attr->ctime;
    }

    return st;
}

//
// This is a typical fuse operation handler; you'll be writing
// a bunch of handlers like it.
//
// A handler takes some arguments
// and supplies either a success or failure response. It provides
// an error response by calling either fuse_reply_err(req, errno), and
// a normal response by calling ruse_reply_xxx(req, ...). The req
// argument serves to link up this response with the original
// request; just pass the same @req that was passed into the handler.
// The @ino argument indicates the file or directory FUSE wants
// you to operate on. It's a 32-bit FUSE identifier; just assign
// it to a yfs_client::inum to get a 64-bit YFS inum.
//
// A file/directory's attributes are a set of information
// including owner, permissions, size, &c. The information is
// much the same as that returned by the stat() system call.
// The kernel needs attributes in many situations, and some
// fuse functions (such as lookup) need to return attributes
// as well as other information, so getattr() gets called a lot.
//
// YFS fakes most of the attributes. It does provide more or
// less correct values for the access/modify/change times
// (atime, mtime, and ctime), and correct values for file sizes.
//


void fuseserver_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    YLOG_INFO("fuse.getattr(ino: %u)", unsigned(ino));

    run(req, [&]{
        auto st = getattr(ino);
        fuse_reply_attr(req, &st, 0);
    });
}

void fuseserver_setattr(fuse_req_t req, fuse_ino_t ino,
        struct stat *attr, int to_set, struct fuse_file_info *fi)
{
    assert(attr);

    YLOG_INFO("fuse.setattr(ino: %u)", unsigned(ino));

    if (FUSE_SET_ATTR_SIZE & to_set) {
        run(req, [&]{
            fs->setattr(ino, attr->st_size);

            fuseserver_getattr(req, ino, fi);
        });
    } else {
        fuse_reply_err(req, ENOSYS);
    }
}

//
// Read up to @size bytes starting at byte offset @off in file @ino.
//
// Ignore @fi.
// @req identifies this request, and is used only to send a
// response back to fuse with fuse_reply_buf or fuse_reply_err.
//
// Read should return exactly @size bytes except for EOF or error.
// In case of EOF, return the actual number of bytes
// in the file.
//
void fuseserver_read(fuse_req_t req, fuse_ino_t ino, size_t size,
      off_t off, struct fuse_file_info *fi)
{
    YLOG_INFO("fuse.read(ino: %u, off: %u, size: %u)",
            unsigned(ino),
            unsigned(off),
            unsigned(size));

    run(req, [&]{
        auto buf = fs->read(ino, off, size);
        fuse_reply_buf(req, buf.data(), buf.size());
    });
}

//
// Write @size bytes from @buf to file @ino, starting
// at byte offset @off in the file.
//
// If @off + @size is greater than the current size of the
// file, the write should cause the file to grow. If @off is
// beyond the end of the file, fill the gap with null bytes.
//
// Ignore @fi.
//
// @req identifies this request, and is used only to send a
// response back to fuse with fuse_reply_buf or fuse_reply_err.
//
void fuseserver_write(fuse_req_t req, fuse_ino_t ino,
  const char *buf, size_t size, off_t off,
  struct fuse_file_info *fi)
{
    YLOG_INFO("fuse.write(ino: %u, off: %u, size: %u)",
            unsigned(ino),
            unsigned(off),
            unsigned(size));

    run(req, [&]{
        auto n = fs->write(ino, off, std::string_view(buf, size));
        fuse_reply_write(req, n);
    });
}

//
// Create file @name in directory @parent.
//
// - @mode specifies the create mode of the file. Ignore it - you do not
//   have to implement file mode.
// - If a file named @name already exists in @parent, return EXIST.
// - Pick an ino (with type of yfs_client::inum) for file @name.
//   Make sure ino indicates a file, not a directory!
// - Create an empty extent for ino.
// - Add a <name, ino> entry into @parent.
// - On success, store the inum of newly created file into @e->ino,
//   and the new file's attribute into @e->attr. Get the file's
//   attributes with getattr().
//
// @return yfs_client::OK on success, and EXIST if @name already exists.
//
void fuseserver_create(fuse_req_t req, fuse_ino_t parent, const char *name,
   mode_t mode, struct fuse_file_info *fi)
{
    YLOG_INFO("fuse.create(parent: %u, name: %s, mode: %u)",
            unsigned(parent),
            name,
            unsigned(mode));

    struct fuse_entry_param e;
    run(req, [&]{
        auto inode = fs->create(parent, name);

        e.attr_timeout = 0.0;
        e.entry_timeout = 0.0;
        e.generation = 0;
        e.ino = fuse_ino_t(inode);
        e.attr = getattr(inode);

        fuse_reply_create(req, &e, fi);
    });
}

void fuseserver_mknod( fuse_req_t req, fuse_ino_t parent,
    const char *name, mode_t mode, dev_t rdev )
{
    YLOG_INFO("fuse.mknod(parent: %u)", unsigned(parent));

    fuse_reply_err(req, ENOSYS);
}

//
// Look up file or directory @name in the directory @parent. If @name is
// found, set e.attr (using getattr) and e.ino to the attribute and inum of
// the file.
//
void fuseserver_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    struct fuse_entry_param e;
    // In yfs, timeouts are always set to 0.0, and generations are always set to 0
    e.attr_timeout = 0.0;
    e.entry_timeout = 0.0;
    e.generation = 0;

    YLOG_INFO("fuse.lookup(parent: %u, name: %s)", unsigned(parent), name);

    run(req, [&]{
        auto inum = fs->lookup(parent, name);
        if (!inum) {
            fuse_reply_err(req, ENOENT);
            return;
        }

        e.ino = inum.value();
        e.attr = getattr(inum.value());
        fuse_reply_entry(req, &e);
    });
}

struct dirbuf {
    char *p;
    size_t size;
};

void dirbuf_add(struct dirbuf *b, const char *name, fuse_ino_t ino)
{
    struct stat stbuf;
    size_t oldsize = b->size;
    b->size += fuse_dirent_size(strlen(name));
    b->p = (char *) realloc(b->p, b->size);
    memset(&stbuf, 0, sizeof(stbuf));
    stbuf.st_ino = ino;
    fuse_add_dirent(b->p + oldsize, name, &stbuf, b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
          off_t off, size_t maxsize)
{
  if ((size_t)off < bufsize)
    return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, NULL, 0);
}

//
// Retrieve all the file names / i-numbers pairs
// in directory @ino. Send the reply using reply_buf_limited.
//
// Call dirbuf_add(&b, name, inum) for each entry in the directory.
//
// NOTE: i don't know what the size/off means, so i returns all entries at once
void fuseserver_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
          off_t off, struct fuse_file_info *fi)
{
    YLOG_INFO("fuse.readdir(dir: %u, size: %u, off: %u)",
            unsigned(ino),
            unsigned(size),
            unsigned(off));

    const yfs::inum_t inum = ino;

    if(!fs->isdir(inum)){
        fuse_reply_err(req, ENOTDIR);
        return;
    }

    run(req, [&]{
        auto entries = fs->readdir(inum, 0, std::numeric_limits<int>::max());
        struct dirbuf b {};
        for (auto& [name, i]: entries) {
            dirbuf_add(&b, name.c_str(), i);
        }

        reply_buf_limited(req, b.p, b.size, off, size);
        free(b.p);
    });
}

void fuseserver_open(fuse_req_t req, fuse_ino_t ino,
     struct fuse_file_info *fi)
{
    fuse_reply_open(req, fi);
}

void fuseserver_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
     mode_t mode)
{
    YLOG_INFO("fuse.mkdir(parent: %u, name: %s, mode: %u)",
            unsigned(parent),
            name,
            unsigned(mode));

    run(req, [&]{
        struct fuse_entry_param e;

        auto inode = fs->mkdir(parent, name);

        e.attr_timeout = 0.0;
        e.entry_timeout = 0.0;
        e.generation = 0;
        e.ino = fuse_ino_t(inode);
        e.attr = getattr(inode);

        fuse_reply_entry(req, &e);
    });
}

void fuseserver_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    YLOG_INFO("fuse.unlink(parent: %llu, name: %s)", yfs::inum_t(parent), name);

    run(req, [&]{
        yfs::inum_t inum = parent;
        const auto ok = fs->unlink(inum, std::string_view(name));
        if (ok) {
            fuse_reply_err(req, 0);
        } else {
            YLOG_WARN("fuse.unlink(failed: no_entry, parent: %llu, name: %s)", inum, name);
            fuse_reply_err(req, ENOENT);
        }
    });
}

void fuseserver_statfs(fuse_req_t req)
{
  struct statvfs buf;

  printf("statfs\n");

  memset(&buf, 0, sizeof(buf));

  buf.f_namemax = 255;
  buf.f_bsize = 512;

  fuse_reply_statfs(req, &buf);
}

struct fuse_lowlevel_ops fuseserver_oper;

int main(int argc, char *argv[])
try {
  char *mountpoint = 0;
  int err = -1;
  int fd;

  setvbuf(stdout, NULL, _IONBF, 0);

  if(argc != 4){
    fprintf(stderr, "Usage: yfs_client <mountpoint> <port-extent-server> <port-lock-server>\n");
    exit(1);
  }
  mountpoint = argv[1];

  srandom(getpid());

  myid = random();

  fs.reset(new yfs::yfs_client(argv[2], argv[3]));

  fuseserver_oper.getattr    = fuseserver_getattr;
  fuseserver_oper.statfs     = fuseserver_statfs;
  fuseserver_oper.readdir    = fuseserver_readdir;
  fuseserver_oper.lookup     = fuseserver_lookup;
  fuseserver_oper.create     = fuseserver_create;
  fuseserver_oper.mknod      = fuseserver_mknod;
  fuseserver_oper.open       = fuseserver_open;
  fuseserver_oper.read       = fuseserver_read;
  fuseserver_oper.write      = fuseserver_write;
  fuseserver_oper.setattr    = fuseserver_setattr;
  fuseserver_oper.unlink     = fuseserver_unlink;
  fuseserver_oper.mkdir      = fuseserver_mkdir;

  const char *fuse_argv[20];
  int fuse_argc = 0;
  fuse_argv[fuse_argc++] = argv[0];
#ifdef __APPLE__
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "nolocalcaches"; // no dir entry caching
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "daemon_timeout=86400";
#endif

  // everyone can play, why not?
  //fuse_argv[fuse_argc++] = "-o";
  //fuse_argv[fuse_argc++] = "allow_other";

  fuse_argv[fuse_argc++] = mountpoint;
  fuse_argv[fuse_argc++] = "-d";

  fuse_args args = FUSE_ARGS_INIT( fuse_argc, (char **) fuse_argv );
  int foreground;
  int res = fuse_parse_cmdline( &args, &mountpoint, 0 /*multithreaded*/,
        &foreground );
  if( res == -1 ) {
    fprintf(stderr, "fuse_parse_cmdline failed\n");
    return 0;
  }

  args.allocated = 0;

  fd = fuse_mount(mountpoint, &args);
  if(fd == -1){
    fprintf(stderr, "fuse_mount failed\n");
    exit(1);
  }

  struct fuse_session *se;

  se = fuse_lowlevel_new(&args, &fuseserver_oper, sizeof(fuseserver_oper),
       NULL);
  if(se == 0){
    fprintf(stderr, "fuse_lowlevel_new failed\n");
    exit(1);
  }

  struct fuse_chan *ch = fuse_kern_chan_new(fd);
  if (ch == NULL) {
    fprintf(stderr, "fuse_kern_chan_new failed\n");
    exit(1);
  }

  fuse_session_add_chan(se, ch);
  // err = fuse_session_loop_mt(se);   // FK: wheelfs does this; why?
  err = fuse_session_loop(se);

  fuse_session_destroy(se);
  close(fd);
  fuse_unmount(mountpoint);

  return err ? 1 : 0;
} catch (std::exception& e) {
    YLOG_ERROR("Init failed: %s", e.what());
    return -1;
}
