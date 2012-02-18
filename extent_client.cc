// RPC stubs for clients to talk to extent_server

#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "extent_client.h"
#include "log.h"

using namespace yfs;

namespace {

inline sockaddr_in make_sockaddr(const std::string& addr)
{
    if (addr.empty()) {
        throw std::invalid_argument("addr is empty");
    }

    sockaddr_in dstsock;
    make_sockaddr(addr.c_str(), &dstsock);

    return dstsock;
}

}

extent_client::extent_client(const std::string& addr)
    : rpc_(std::make_unique<rpcc>(make_sockaddr(addr)))
{
    if (rpc_->bind() != 0) {
        throw Io_error("bind failed to: " + addr);
    }
}

std::optional<std::string> extent_client::get(const extent_protocol::extentid_t id)
{
    std::string buf;
    auto rc = rpc_->call(extent_protocol::get, id, buf);
    if (rc == extent_protocol::NOENT) {
        return {};
    } else if (rc != extent_protocol::OK) {
        YLOG_ERROR("ext::getFailed(rc: %d)", rc);
        throw Io_error{"Get extent failed"};
    } else {
        return std::optional(std::move(buf));
    }
}

std::optional<extent_protocol::attr> extent_client::getattr(const extent_protocol::extentid_t id)
{
    extent_protocol::attr attr {};

    auto rc = rpc_->call(extent_protocol::getattr, id, attr);
    if (rc == extent_protocol::NOENT) {
        return {};
    } else if (rc != extent_protocol::OK) {
        YLOG_ERROR("ext::getattrFailed(rc: %d, id: %d)", rc, int(id));
        throw Io_error{"Getattr failed}"};
    } else {
        return std::optional(std::move(attr));
    }
}

void extent_client::put(const extent_protocol::extentid_t id, const std::string& buf)
{
    int r {};
    int rc = rpc_->call(extent_protocol::put, id, buf, r);
    if (rc != extent_protocol::OK) {
        YLOG_ERROR("ext::put(rc: %d, id: %d, size: %d)", rc, int(id), int(buf.size()));
        throw Io_error{"PutExt failed"};
    }
}

void extent_client::remove(const extent_protocol::extentid_t id)
{
    int r {};
    int ret = rpc_->call(extent_protocol::remove, id, r);
    if (ret != extent_protocol::OK) {
        YLOG_ERROR("ext::remove(rc: %d, id: %d)", ret, int(id));
        throw Io_error{"RemoveExt failed"};
    }
}
