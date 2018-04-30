#ifndef yfs_client_h
#define yfs_client_h

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <stdexcept>
#include "extent_client.h"
#include "lock_client.h"

namespace yfs {

typedef unsigned long long inum_t;

inline constexpr extent_protocol::extentid_t super_block_id = 0;
inline constexpr extent_protocol::extentid_t root_id = 1;

struct No_entry_error : std::runtime_error {
    using runtime_error::runtime_error;
};

struct Already_exist_error : std::runtime_error {
    using runtime_error::runtime_error;
};

// execution an operation on a invalid kind of file
struct Invalid_op_error : std::runtime_error {
    using runtime_error::runtime_error;
};

struct Corrupt_error : std::runtime_error {
    using runtime_error::runtime_error;
};

struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
};

struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
};

struct dirent {
    std::string name;
    inum_t inum;
};

class Super_block {
public:
    enum Inode_type {
        dir, file
    };

    // create a new super block
    explicit Super_block(extent_client& client);
    Super_block(extent_client& client, const std::string& extent);

    ~Super_block();

    inum_t alloc_inum(Inode_type _type);

    void commit();

private:
    extent_client& client_;
    inum_t         next_inum_;
    bool           dirty_ = false;
};

class File_block {
public:
    // create a new file
    File_block(extent_client& client, inum_t inum);
    File_block(extent_client& client, extent_protocol::extentid_t id, const std::string& extent);

    ~File_block();

    inum_t inode_num() const;

    fileinfo getfile();

    // @pre offset >= 0
    // @pre limit >= 0
    std::string read(int offset, int limit);

    // @return number of bytes written
    // @pre offset >= 0
    int write(int offset, const std::string_view content);

    void truncate(int len);

    void commit();

private:
    extent_client&  client_;
    const extent_protocol::extentid_t eid_;
    std::string     extent_;
    bool            dirty_ = false;
};

class Dir_block {
public:
    // create new dir
    Dir_block(extent_client& client, inum_t inum);
    Dir_block(extent_client& client, extent_protocol::extentid_t id, const std::string& extent);

    ~Dir_block();

    inum_t inode_num() const;

    std::vector<dirent> readdir(int offset, int limit);

    std::optional<inum_t> lookup(const std::string& name);

    void insert(const std::string& name, inum_t inum);
    void remove(const std::string_view name);

    dirinfo getdir();

    void commit();

private:
    std::map<std::string, inum_t> load_(const std::string& content);

    std::string encode_();

private:
    extent_client& client_;
    const extent_protocol::extentid_t eid_;
    std::map<std::string, inum_t> children_;
    bool     dirty_ = false;
};

// @throws all functions will throw exceptions above.
// all member functions after construction are of strong guarantee
class yfs_client {
public:
    yfs_client(const std::string& extent_dst, const std::string& lock_dst);
    ~yfs_client();

    bool isfile(inum_t);
    bool isdir(inum_t);

    std::optional<fileinfo> getfile(inum_t inode);
    std::optional<dirinfo>  getdir(inum_t inode);

    inum_t create(inum_t parent, const std::string& name);
    inum_t mkdir(inum_t parent, const std::string& name);

    std::optional<inum_t> lookup(inum_t parent, const std::string& name);

    bool unlink(inum_t parent, const std::string_view name);

    // @pre offset >= 0
    // @pre limit >= 0
    std::vector<dirent> readdir(inum_t parent, int offset, int limit);

    void setattr(inum_t file, unsigned long long size);

    std::string read(inum_t inum, int offset, int limit);

    // @return number of bytes written
    int write(inum_t inum, int offset, const std::string_view content);

private:
    void load_or_init_fs_();

    Super_block get_super_block_();
    std::optional<File_block> get_file_(inum_t inum);
    std::optional<Dir_block>  get_dir_(inum_t inum);

private:
    const std::unique_ptr<extent_client> extent_client_;
    const std::unique_ptr<lock_client>   lock_client_;
};

class Duplicate_lock_error : std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

// @throws Io_error if communication fails.
class Multi_lock {
public:
    explicit Multi_lock(lock_client& client);
    ~Multi_lock();

    // @throws Duplicate_lock_error if double lock
    void add_lock(lock_protocol::lockid_t lock);

private:
    lock_client&                         client_;
    std::vector<lock_protocol::lockid_t> locks_;
};

}

#endif
