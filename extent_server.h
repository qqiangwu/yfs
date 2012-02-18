// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <unordered_map>
#include <mutex>
#include "extent_protocol.h"

class extent_server {
public:
    extent_server();

    int put(extent_protocol::extentid_t id, std::string, int &);
    int get(extent_protocol::extentid_t id, std::string &);
    int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
    int remove(extent_protocol::extentid_t id, int &);

private:
    std::mutex mutex_;
    std::unordered_map<extent_protocol::extentid_t, std::string> extents_;
    std::unordered_map<extent_protocol::extentid_t, extent_protocol::attr> attrs_;
};

#endif
