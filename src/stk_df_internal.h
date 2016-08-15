#ifndef STK_DF_INTERNAL_H
#define STK_DF_INTERNAL_H

/* Helpful socket defs */
#define set_sock_buf_nice(_socket, _level, _option_name, _bufsize, _port, _env) do { \
	int _rc, _buf = _bufsize;                                                        \
	while(_buf > 65536) {                                                            \
		_rc = setsockopt(_socket, _level, _option_name, &_buf, sizeof(_buf));        \
		if (_rc == 0) break;                                                         \
		if (_buf == _bufsize) STK_LOG(STK_LOG_ERROR,"Failed to set %s socket buffer size %d on port %d, env %p err %d df %d", \
			_option_name == SO_RCVBUF ? "receive" : "send", _buf, _port, _env, errno, _socket);\
		_buf /= 2;                                                                   \
	}                                                                                \
	if(_buf != _bufsize && _rc == 0)                                                 \
		STK_LOG(STK_LOG_NORMAL,"Set socket %s buffer size %d on port %d, env %p",    \
			_option_name == SO_RCVBUF ? "receive" : "send", _buf, _port, _env);      \
} while(0)

/* Common data flow defines - needed in a separate header for testing */
#define STK_DATA_FLOW_CLIENTIP_ID 0x8110
#define STK_DATA_FLOW_CLIENT_PROTOCOL_ID 0x8120

#endif
