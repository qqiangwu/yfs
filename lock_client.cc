// RPC stubs for clients to talk to lock_server

#include "lock_client.h"
#include "rpc.h"
#include <arpa/inet.h>

#include <stdio.h>
#include <stdexcept>
#include <sstream>
#include <iostream>

lock_client::lock_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() < 0) {
    throw std::runtime_error("lock_client: call bind");
  }
}

int
lock_client::stat(lock_protocol::lockid_t lid)
{
  int r;
  lock_protocol::status ret = cl->call(lock_protocol::stat, cl->id(), lid, r);
  VERIFY (ret == lock_protocol::OK);
  return r;
}

lock_protocol::status
lock_client::acquire(lock_protocol::lockid_t lid)
{
    int r;
    return cl->call(lock_protocol::acquire, cl->id(), lid, r);
}

lock_protocol::status
lock_client::release(lock_protocol::lockid_t lid)
{
    int r;
    return cl->call(lock_protocol::release, cl->id(), lid, r);
}
