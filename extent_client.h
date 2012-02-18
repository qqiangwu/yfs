// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include <stdexcept>
#include <optional>
#include <memory>
#include "extent_protocol.h"
#include "rpc.h"

namespace yfs {

class Io_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// strong guarantee
// @throws Io_error if failed to connect server
class extent_client {
public:
    // @throws std::invalid_argument
    extent_client(const std::string& addr);

    std::optional<std::string> get(extent_protocol::extentid_t eid);
    std::optional<extent_protocol::attr> getattr(extent_protocol::extentid_t eid);
    void put(extent_protocol::extentid_t eid, const std::string& buf);
    void remove(extent_protocol::extentid_t eid);

private:
    std::unique_ptr<rpcc> rpc_;
};

}

#endif
