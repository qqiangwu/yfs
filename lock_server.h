// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"
#include <string>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

class lock_server {

 protected:
  int nacquire;

 public:
  lock_server();
  ~lock_server() {};

  lock_protocol::status stat(int client_id, lock_protocol::lockid_t lid, int&);
  lock_protocol::status acquire(int client_id, lock_protocol::lockid_t lid, int&);
  lock_protocol::status release(int client_id, lock_protocol::lockid_t lid, int&);

private:
    std::mutex mutex_;
    std::condition_variable lock_released_;
    std::unordered_map<lock_protocol::lockid_t, int/*client_id*/> locks_;
};

#endif







