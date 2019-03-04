#pragma once

#include "log.h"
#include <nanomsg/nn.h>
#include <nanomsg/reqrep.h>

#include <memory>
#include <string>

#include <cstring>

#include <confuse.h>

#include "Resolver.h"
#include "singleton.h"

class _dispatcher {
	int s;
	int stop_event_fd;

	int process_peer(char *msg, int len);
	void str2reply(char *&msg,int &len,const std::string &s,int code);
	void make_reply(char *&msg,int &len,const CDriver::SResult_t &r);
	void create_reply(char *&msg, int &len, const char *req, int req_len);
	void create_error_reply(char *&msg, int &len,std::string description,int code=1);

  protected:
	void dispose() {}
  public:
	_dispatcher();

	void loop();
	void stop();
};

typedef singleton<_dispatcher> dispatcher;
