/*
 * Copyright (c) 2023 Pit Suwongs, พิทย์ สุวงศ์ (Thailand)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * By Pit Suwongs <admin@ornpit.com>
 */

#ifndef AXISHTTPSOCK_H_
#define AXISHTTPSOCK_H_

/**
* @file axishttpsock.h
* @brief web server 
* 
* requirement 
* - C++ 11 or libpthread 
* 
* features
* - written in C/C++
* - multi-threading using libpthread or stdthread (C11)
* - support multi-platform, resuming, multi-ranges for video streaming
*/

//#define _PTHREAD
#define ALLOCMEM malloc
#define FREEMEM free
#define CPYMEM memcpy
#define SETMEM memset
#define TIME time_t


#if defined(_PTHREAD)
#include <pthread.h>
#else
#include <atomic>
#include <thread> 
#endif

#include <string.h>
#include <stdio.h>		
#include <stdlib.h>   
#include <ctype.h>
#include <time.h>

#if defined(_MSC_VER)
 //Windows
 //we do not want the warnings about the old deprecated and unsecure CRT functions 
 //since these examples can be compiled under *nix as well
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <sys/timeb.h>
#include <sys/stat.h>
#include <stdint.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma warning( disable : 4996 )				// disable deprecated warning

#define FILEPTR FILE*
#define SOCKET SOCKET
#define CLOSESOCKET closesocket

#if defined(_M_X64) 
#define FOPEN fopen
#define FWRITE fwrite
#define FREAD fread
#define FSEEK _fseeki64
#define FTELL _ftelli64
#define FFLUSH fflush
#define FCLOSE fclose
#define X64
#else
#define FOPEN fopen
#define FWRITE fwrite
#define FREAD fread
#define FSEEK fseek
#define FTELL ftell
#define FFLUSH fflush
#define FCLOSE fclose
#define X32
#endif
#define FEOF feof
#define SEPERATORCHAR '\\'
#define SEPERATORSTR "\\"
#define BYTE __uint8_t
#define WORD __uint16_t
#define DWORD __uint32_t
#define PACKED

#else 

#if defined(__APPLE__)
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <netdb.h>
#include <sys/socket.h> 
#include <arpa/inet.h>  
#include <sys/time.h>
#include <utime.h>
#include <unistd.h>     
#include <signal.h>

#else	
//linux
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h> 
#include <arpa/inet.h>  
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/mman.h>	
#include <utime.h>
#include <unistd.h>  
#include <signal.h>	
#endif

#if defined(__arm__) || defined(__aarch64__)
#define X64
#define FTELL ftello
#define FSEEK fseeko
#else
#define X32
#define FTELL ftell
#define FSEEK fseek
#endif

#define FILEPTR FILE*
#define SOCKET int
#define CLOSESOCKET close
#define FOPEN fopen
#define FREAD fread
#define FFLUSH fflush
#define FWRITE fwrite
#define FCLOSE fclose
#define FEOF feof
#define SEPERATORCHAR '/'
#define SEPERATORSTR "/"
#define BYTE __uint8_t
#define WORD __uint16_t
#define DWORD __uint32_t
#define PACKED

#endif

#pragma pack(push,1)

typedef struct HTTPNetworkS* HTTPNetwork;
typedef struct HTTPNetworkConfigS* HTTPNetworkConfig;
typedef struct HTTPListenThreadS* HTTPListenThread;
typedef struct HTTPProcThreadS* HTTPProcThread;

//send and disconnect
#define HTTPMSGEND 0
//send and continue 
#define HTTPMSGCONTINUE 1

/**
* main function
* @return HTTPMSGEND HTTPMSGCONTINUE 
*/
typedef int HTTPMsgFunc(HTTPNetwork n);

struct HTTPQueue
{
	unsigned int size;
	unsigned int mask;
	unsigned int entries;
	unsigned int first;
	unsigned int last;
	HTTPNetwork* data;
} PACKED;

#define HTTPSTATEINIT 0
#define HTTPSTATECONTINUE 1
#define HTTPSTATEDESTROY -1

/**
* Client network structure
*/
struct HTTPNetworkS
{
 SOCKET socket;
 struct sockaddr addr;
 /**  
 * last time that client send msg to server
 */
 TIME lastping;	
 /**
  * receive buffer
  */
 char* buffer;
 size_t bufferSize;
 size_t bufferIndex;
 /**
 * send buffer
 */
 char* sendmsg;
 size_t sendSize;
 size_t sendIndex;
 int state;
 void* data;
} PACKED;

/**
* Main Structure
*/
struct HTTPNetworkConfigS
{
	/**
	* Only one reading network thread
	*/
	HTTPListenThread listenthread;
	/**
	* num of active proc thread
	*/
#if defined(_PTHREAD)
	unsigned int numprocthread;
	pthread_mutex_t mutex;
#else
	std::atomic_int numprocthread;
#endif
	/**
	* queue
	*/
	HTTPQueue queue;
	/**
	* message function
	*/
	HTTPMsgFunc* msgFunc; 
	/**
	* Exit signal
	*/
	bool exitflag;
	/**
	* max user for the whole system
	*/
	unsigned int maxconnection;
	/**
	* ping time out
	*/
	unsigned int pingtimeout;
	/**	
	* msg size
	*/
	unsigned int msgsize;
	/**
	* maximum msg size
	*/
	unsigned int maxmsgsize;
} PACKED;

/**
* Thread data for reading
*/
struct HTTPListenThreadS
{
	/**
	* server address
	*/
	SOCKET server;
	/**
	* port
	*/
	int port;
	/**
	* the maximum tick per loop for optimize
	*/
	unsigned int tick;
	/**
	* tick helper
	*/
	TIME lasttime;
	/**
	* Config 
	*/
	HTTPNetworkConfig config;
} PACKED;

struct HTTPProcThreadS
{
	HTTPNetwork n;
	HTTPNetworkConfig config;
} PACKED;

#pragma pack(pop)


/**
* HTTP Network class
*/
class HTTPServerNetwork
{
private:
	HTTPNetworkConfig config;
public:
	/**
	* Maximum thread
	* Default 1024
	*/
	static unsigned int MAXCONNECTION;
	/**
	* Default 20
	*/
	static unsigned int MAXLISTEN;
	/**
	* Default 100000
	*/
	static unsigned int MAXQUEUE;
	/**
	* Default 100000
	*/
	static unsigned int MAXMSG;
	/*
	* Default 2048
	*/
	static unsigned int MSGSIZE;
	/**
	* socket buffer
	* Default 1000000
	*/
	static unsigned int MAXBUFFER;
	/**
	* Default 20000 millisec
	*/
	static unsigned int PINGTIMEOUT;
	/**
	* @param port 
	*/
	HTTPServerNetwork(int port);
	/**
	* set incoming callback 
	*/
	void setOnMsg(HTTPMsgFunc* f);
	/**
	* set file path
	*/
	void setPath(const char* path);
	/**
	* Destructor
	*/
    ~HTTPServerNetwork();
	/**
	* forever loop
	*/
	void begin();
	/**
	* exit
	*/
	void exit();
};

/**
* check if the header is ready
*/
bool httpisheadercomplete(HTTPNetwork n);
/*
* get the length
* 
*/
bool httpgetcontentlength(HTTPNetwork n, size_t* len);
/**
* get the contenttype
* 
*/
bool httpgetcontenttype(HTTPNetwork n, char* type, char* boundary, size_t len);

#define HTTPMETHODGET 1
#define HTTPMETHODPOST 2
#define HTTPMETHODPATCH 3
#define HTTPMETHODDELETE 4

/**
* 
*/
bool httpgetpath(HTTPNetwork n, char* path, size_t len);

/**
* @return -1 for unknown 
*/
int httpgetmethod(HTTPNetwork n);

int httpgetnumparam(HTTPNetwork n);
size_t httpgetparamvalue(HTTPNetwork n, const char* name, char* value, size_t len);
bool httpgetparam(HTTPNetwork n, int id, char* name, char* value, size_t len);

int httppostnumparam(HTTPNetwork n);
bool httppostparam(HTTPNetwork n, int id, char* name, char* value, size_t len);
size_t httppostparamvalue(HTTPNetwork n, const char* name, char* value, size_t len);

/**
* @param total the total size
* @return num of range
*/
size_t httpgetranges(HTTPNetwork n, size_t** range, size_t total);

/**
* simple single function for small message
* @param content simple text
*/
void httpsettext(HTTPNetwork n, const char* content);
void httpsetjson(HTTPNetwork n, const char* content);
void httpsetjson(HTTPNetwork n, unsigned int code,const char* content);

/**
* simple single function for small data (<1mb)
* @param header can be NULL, not ending with \r\n
* @param content
* @param total the total bytes of content
*/
void httpsetcontent(HTTPNetwork n, const char* header, const char* contenttype, char* content, size_t total);
void httpsetcontentwithcode(HTTPNetwork n, unsigned int code, const char* header, const char* contenttype, char* content, size_t total);

/**
* allocate the buffer for the response 
* @param total + 2000 bytes more for boundary (padding buffer)
*/
void httpset(HTTPNetwork n,size_t total);

/**
* for large data (>1mb)
* simple & single large binary (ranges not support)
* httpset, httpsetnormal then content body 
* must call httpsetfooter at the end
* @param header can be NULL, not ending with \r\n
* @param total the total bytes
*/
void httpsetraw(HTTPNetwork n, const char* header, const char* contenttype, size_t total);

/**
* for large data (>1mb) that support ranges
* httpset, httpsetacceptranges then content body 
* must call httpsetfooter at the end
* @param header can be NULL, not ending with \r\n
* @param total the total bytes
*/
void httpsetacceptranges(HTTPNetwork n, const char* header, const char* contenttype, size_t total);

/**
* for large data (>1mb) that support ranges
* httpset, httpsetsingleranges then content body
* must call httpsetfooter at the end
* @param header can be NULL, not ending with \r\n
* @param total the total bytes
*/
void httpsetsingleranges(HTTPNetwork n, const char* header, const char* contenttype, size_t total, size_t rangea, size_t rangeb);

/**
* for large data (>1mb)  that support ranges
* httpset, httpsetmultiranges then httpbeginboundarymultiranges, body and httpendboundarymultiranges
* @param header can be NULL, not ending with \r\n
* @param total the total bytes
* @param boundary must be < 16 bytes
*/
void httpsetmultiranges(HTTPNetwork n, const char* header, const char* contenttype, size_t total, int numrange, size_t* range, const char* boundary);

/**
* must be called after httpsetmultiranges
* @param boundary must be < 16 bytes
*/
void httpbeginboundarymultiranges(HTTPNetwork n, const char* contenttype, const char* boundary, size_t size, size_t rangea, size_t rangeb);

/**
* must be called after httpbeginboundarymultiranges
* @param boundary must be < 16 bytes
*/
void httpendboundarymultiranges(HTTPNetwork n, const char* boundary);

/**
* must be called after 
* httpsetcontent or 
* httpsetacceptranges or
* httpsetsingleranges
* 
*/
void httpendfooter(HTTPNetwork n);

void httpgetclientaddr(HTTPNetwork n, char* out);

/**
* Content-Disposition: form-data; name="uploadedfile"; filename="hello.o"
*/
bool httpgetfilename(char* buffer, char* filename, size_t len);

/**
* shift receiving buffer
*/
void httpshiftbuffer(HTTPNetwork n, size_t len);

/**
* expand receiving buffer
*/
bool httpexpandbuffer(HTTPNetwork msg, size_t len);

/**
* @param header at least 256 bytes
*/
void httplastmodified(TIME lastmodified, char* header);

/**
* @param header at least 256 bytes
*/
bool httpcontenttype(const char* filename, char* contenttype);

/**
* @param status at lease 256 bytes
*/
bool httpcode(unsigned int code, char* status);

#endif
