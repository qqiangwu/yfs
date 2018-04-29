// yfs client.  implements FS operations using extent and lock server
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "yfs_client.h"
#include "log.h"

using namespace yfs;

inline constexpr extent_protocol::extentid_t file_inode_mask = 0x80000000;

Super_block::Super_block(extent_client& client, const std::string& extent)
try
    : client_(client),  next_inum_(std::stoull(extent))
{
} catch (std::invalid_argument&) {
    throw Corrupt_error{"super block corrupt"};
}

Super_block::Super_block(extent_client& client)
    : client_(client), next_inum_(root_id), dirty_(true)
{
}

Super_block::~Super_block()
{
    assert(!dirty_);
}

inum_t Super_block::alloc_inum(Super_block::Inode_type type)
{
    if (next_inum_ >= file_inode_mask) {
        throw Io_error{"run out of inode space"};
    }

    dirty_ = true;
    const auto r = next_inum_++;

    return (type == file)? (r | file_inode_mask): r;
}

void Super_block::commit()
{
    if (!dirty_) {
        return;
    }

    auto buf = std::to_string(next_inum_);
    client_.put(super_block_id, buf);

    dirty_ = false;
}

File_block::File_block(extent_client& client, inum_t inum)
    : client_(client), eid_(inum & ~file_inode_mask), dirty_(true)
{
    YLOG_INFO("file.new(inum: %llu)", inum);
}

File_block::File_block(extent_client& client, extent_protocol::extentid_t id, const std::string& extent)
    : client_(client), eid_(id), extent_(extent)
{
}

File_block::~File_block()
{
    assert(!dirty_);
}

inum_t File_block::inode_num() const
{
    return static_cast<inum_t>(eid_ | file_inode_mask);
}

fileinfo File_block::getfile()
{
    const auto attropt = client_.getattr(eid_);
    if (attropt) {
        fileinfo info;

        auto& attr = attropt.value();
        info.atime = attr.atime;
        info.mtime = attr.mtime;
        info.ctime = attr.ctime;
        info.size = attr.size;

        return info;
    } else {
        throw No_entry_error{std::to_string(eid_)};
    }
}

std::string File_block::read(const int offset, const int limit)
{
    assert(offset >= 0);
    assert(limit >= 0);

    std::string::size_type beg = offset;
    if (beg >= extent_.size()) {
        return "";
    }

    const auto end = std::min(beg + limit, extent_.size());
    auto ret = extent_.substr(beg, end - beg);

    // submit
    return ret;
}

int File_block::write(const int offset, const std::string_view content)
{
    assert(offset >= 0);

    if (content.empty()) {
        return 0;
    }

    const auto new_len = offset + content.size();
    extent_.resize(new_len);
    std::copy(content.begin(), content.end(), extent_.begin() + offset);

    dirty_ = true;

    return content.size();
}

void File_block::truncate(const int len)
{
    assert(len >= 0);

    extent_.resize(len);
    dirty_ = true;
}

void File_block::commit()
{
    if (!dirty_) {
        return;
    }

    YLOG_INFO("file.commit(extent: %llu)", eid_);
    client_.put(eid_, extent_);
    dirty_ = false;
}

Dir_block::Dir_block(extent_client& client, const inum_t inum)
    : client_(client), eid_(inum), dirty_(true)
{
    YLOG_INFO("dir.new(id: %llu)", inum);
}

Dir_block::Dir_block(extent_client& client, extent_protocol::extentid_t id, const std::string& extent)
    : client_(client), eid_(id), children_(load_(extent))
{
}

Dir_block::~Dir_block()
{
    assert(!dirty_);
}

std::map<std::string, inum_t> Dir_block::load_(const std::string& content)
{
    std::istringstream iss(content);
    std::map<std::string, inum_t> entries;

    std::string name;
    inum_t      inod {};
    while (iss >> name >> inod) {
        entries.emplace(std::move(name), inod);
    }
    if (!iss.eof()) {
        throw Corrupt_error{"dir block corrupted"};
    }

    return entries;
}

inum_t Dir_block::inode_num() const
{
    return eid_;
}

std::vector<dirent> Dir_block::readdir(const int offset, const int limit)
{
    assert(offset >= 0);
    assert(limit > 0);

    if (unsigned(offset) >= children_.size()) {
        return {};
    }

    std::vector<dirent> result;
    result.reserve(std::min<int>(limit, children_.size()));

    auto iter = std::next(children_.begin(), offset);
    auto i = 0;
    while (iter != children_.end() && i < limit) {
        result.push_back({iter->first, iter->second});

        ++i;
        ++iter;
    }

    return result;
}

std::optional<inum_t> Dir_block::lookup(const std::string& name)
{
    auto iter = children_.find(name);

    return iter == children_.end()?
        std::nullopt:
        std::optional(iter->second);
}

void Dir_block::insert(const std::string& name, const inum_t inum)
{
    if (name.empty()) {
        throw std::invalid_argument{"bad file name"};
    }

    auto iter = children_.find(name);
    if (iter != children_.end()) {
        YLOG_ERROR("dir.insertFailed(name: %s, inum: %u)", name.c_str(), unsigned(inum));
        throw Already_exist_error{"dir entry exists"};
    }

    children_.emplace(name, inum);
    dirty_ = true;
}

void Dir_block::remove(const std::string_view name)
{
    const auto erased = children_.erase(std::string(name));
    if (erased > 0) {
        dirty_ = true;
    }
}

dirinfo Dir_block::getdir()
{
    auto attropt = client_.getattr(eid_);
    if (!attropt) {
        YLOG_ERROR("dir.getattrFailed(inum: %u)", unsigned(eid_));
        throw No_entry_error{"dir not found"};
    }

    const auto& attr = attropt.value();
    dirinfo info;
    info.atime = attr.atime;
    info.mtime = attr.mtime;
    info.ctime = attr.ctime;

    return info;
}

void Dir_block::commit()
{
    if (!dirty_) {
        return;
    }

    YLOG_INFO("dir.commit(extent: %llu)", eid_);

    auto content = encode_();
    client_.put(eid_, content);

    dirty_ = false;
}

std::string Dir_block::encode_()
{
    std::ostringstream oss;

    for (auto& [k, v]: children_) {
        oss << k << ' ' << v << std::endl;
    }

    return oss.str();
}

///////////////////////////////////////////////////
// Facade layer
yfs_client::yfs_client(const std::string& extent_dst, const std::string& lock_dst)
    : extent_client_(new extent_client(extent_dst))
{
    YLOG_INFO("Yfs(extent: %s): setup", extent_dst.c_str());
    load_or_init_fs_();
}

yfs_client::~yfs_client()
{
}

void yfs_client::load_or_init_fs_()
{
    auto contentopt = extent_client_->get(super_block_id);
    if (contentopt) {
        return;
    }

    YLOG_INFO("yfs not found: init", 0);
    Super_block sb(*extent_client_);
    Dir_block root(*extent_client_, sb.alloc_inum(Super_block::dir));

    assert(root.inode_num() == root_id);

    root.commit();
    sb.commit();

    YLOG_INFO("yfs init done", 0);
}

bool yfs_client::isfile(const inum_t inum)
{
    return bool(inum & file_inode_mask);
}

bool yfs_client::isdir(const inum_t inum)
{
  return !isfile(inum);
}

std::optional<fileinfo> yfs_client::getfile(const inum_t inum)
{
    YLOG_INFO("getfile(inum: %llu)", inum);

    auto file = get_file_(inum);

    return file? std::optional(file->getfile()): std::nullopt;
}

std::optional<dirinfo> yfs_client::getdir(const inum_t inum)
{
    YLOG_INFO("getdir(inum: %llu)", inum);

    auto dir = get_dir_(inum);

    return dir? std::optional(dir->getdir()): std::nullopt;
}

inum_t yfs_client::create(const inum_t parent, const std::string& name)
{
    YLOG_INFO("create(parent: %llu, name: %s)", parent, name.c_str());

    auto sb = get_super_block_();
    auto dir = get_dir_(parent);
    if (!dir) {
        throw No_entry_error{std::to_string(parent)};
    }

    const auto inode = sb.alloc_inum(Super_block::file);
    auto newfile = File_block(*extent_client_, inode);
    dir->insert(name, inode);

    sb.commit();
    newfile.commit();
    dir->commit();

    return inode;
}

inum_t yfs_client::mkdir(const inum_t parent, const std::string& name)
{
    YLOG_INFO("mkdir(parent: %llu, name: %s)", parent, name.c_str());

    auto sb = get_super_block_();
    auto dir = get_dir_(parent);
    if (!dir) {
        throw No_entry_error{std::to_string(parent)};
    }

    const auto inode = sb.alloc_inum(Super_block::dir);
    auto newdir = Dir_block(*extent_client_, inode);
    dir->insert(name, inode);

    sb.commit();
    newdir.commit();
    dir->commit();

    return inode;
}

bool yfs_client::unlink(const inum_t parent, const std::string_view name)
{
    YLOG_INFO("unlink(parent: %llu, name: %s)", parent, name.data());

    auto dir = get_dir_(parent);
    if (!dir) {
        throw No_entry_error{std::to_string(parent)};
    }

    const auto entry = dir->lookup(std::string(name));
    if (!entry) {
        YLOG_INFO("unlink(parent: %llu, name: %s): no entry", parent, name.data());
        return false;
    } else {
        YLOG_INFO("unlink(parent: %llu, name: %s): done", parent, name.data());
        dir->remove(name);
        dir->commit();
        extent_client_->remove(*entry);
        return true;
    }
}

std::optional<inum_t> yfs_client::lookup(const inum_t parent, const std::string& name)
{
    YLOG_INFO("lookup(parent: %llu, name: %s)", parent, name.c_str());

    auto dir = get_dir_(parent);
    if (!dir) {
        throw No_entry_error{std::to_string(parent)};
    }

    return dir->lookup(name);
}

std::vector<dirent> yfs_client::readdir(const inum_t parent, int offset, int limit)
{
    YLOG_INFO("readdir(parent: %llu, offset: %d, limit: %d)", parent, offset, limit);

    auto dir = get_dir_(parent);
    if (!dir) {
        throw No_entry_error{std::to_string(parent)};
    }

    return dir->readdir(offset, limit);
}

void yfs_client::setattr(const inum_t inum, const unsigned long long size)
{
    YLOG_INFO("setattr(inum: %llu)", inum);

    auto file = get_file_(inum);
    if (!file) {
        throw No_entry_error{std::to_string(inum)};
    }

    file->truncate(size);
    file->commit();
}

std::string yfs_client::read(const inum_t inum, int offset, int limit)
{
    YLOG_INFO("read(inum: %llu, offset: %d, limit: %d)", inum, offset, limit);

    auto file = get_file_(inum);
    if (!file) {
        throw No_entry_error{std::to_string(inum)};
    }

    return file->read(offset, limit);
}

int yfs_client::write(const inum_t inum, int offset, const std::string_view content)
{
    YLOG_INFO("write(inum: %llu, offset: %d, size: %u)", inum, unsigned(offset), unsigned(content.size()));

    auto file = get_file_(inum);
    if (!file) {
        throw No_entry_error{std::to_string(inum)};
    }

    const auto n = file->write(offset, content);

    file->commit();
    return n;
}

Super_block yfs_client::get_super_block_()
{
    auto content = extent_client_->get(super_block_id);
    if (!content) {
        throw Corrupt_error{"superblock cannot find"};
    }

    return Super_block(*extent_client_, content.value());
}

std::optional<File_block> yfs_client::get_file_(inum_t inum)
{
    auto extent_id = inum & ~file_inode_mask;
    auto content = extent_client_->get(extent_id);

    return content? std::optional(File_block{*extent_client_, extent_id, content.value()}): std::nullopt;
}

std::optional<Dir_block>  yfs_client::get_dir_(inum_t inum)
{
    auto content = extent_client_->get(inum);

    return content? std::optional(Dir_block{*extent_client_, inum, content.value()}): std::nullopt;
}
