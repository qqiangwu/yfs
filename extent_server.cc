// the extent server implementation

#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "extent_server.h"
#include "log.h"

extent_server::extent_server() {}

int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
    YLOG_INFO("ext.put(id: %llu, size: %u)", id, unsigned(buf.size()));

    std::lock_guard<std::mutex> lock(mutex_);

    extents_[id] = buf;

    auto now = ::time(NULL);
    auto& attr = attrs_[id];
    attr.ctime = now;
    attr.mtime = now;
    attr.size = buf.size();

    return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
    YLOG_INFO("ext.get(id: %llu)", id);

    std::lock_guard<std::mutex> lock(mutex_);

    auto iter = extents_.find(id);
    if (iter == extents_.end()) {
        return extent_protocol::NOENT;
    } else {
        buf = iter->second;
        return extent_protocol::OK;
    }
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
    YLOG_INFO("ext.getattr(id: %llu)", id);

    std::lock_guard<std::mutex> lock(mutex_);

    auto iter = attrs_.find(id);
    if (iter == attrs_.end()) {
        return extent_protocol::NOENT;
    } else {
        a = iter->second;
        return extent_protocol::OK;
    }
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
    YLOG_INFO("ext.remove(id: %llu)", id);

    std::lock_guard<std::mutex> lock(mutex_);

    extents_.erase(id);
    attrs_.erase(id);

    return extent_protocol::OK;
}
