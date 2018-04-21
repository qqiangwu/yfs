// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status lock_server::acquire(const int client_id, const lock_protocol::lockid_t lid, int&)
{
    std::unique_lock<std::mutex> guard(mutex_);

    for (;;) {
        auto iter = locks_.find(lid);
        if (iter == locks_.end()) {
            locks_.insert(std::make_pair(lid, client_id));
            return lock_protocol::OK;
        } else {
            // note: a client acquire a lock twice, the second acquire must block too
            lock_released_.wait(guard);
        }
    }
}

lock_protocol::status lock_server::release(const int client_id, const lock_protocol::lockid_t lid, int&)
{
    std::lock_guard<std::mutex> guard(mutex_);

    auto iter = locks_.find(lid);
    if (iter != locks_.end() && iter->second == client_id) {
        locks_.erase(iter);
        lock_released_.notify_one();
    }

    return lock_protocol::OK;
}
