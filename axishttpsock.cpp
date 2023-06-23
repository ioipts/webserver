#include "axishttpsock.h"

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__APPLE__)
#define MSG_NOSIGNAL 0
#if !defined(__APPLE__)
#define MSG_DONTWAIT 0
#endif
#endif

unsigned int HTTPServerNetwork::MAXBUFFER=1000000;
unsigned int HTTPServerNetwork::MSGSIZE = 2048;
unsigned int HTTPServerNetwork::MAXMSG = 100000;
unsigned int HTTPServerNetwork::MAXLISTEN = 128;
unsigned int HTTPServerNetwork::MAXCONNECTION = 1024;
unsigned int HTTPServerNetwork::MAXQUEUE=100000;
unsigned int HTTPServerNetwork::PINGTIMEOUT = 3000;

bool inithttpqueue(HTTPQueue* p, unsigned int size);
int httpenqueue(HTTPQueue* h, HTTPNetwork value);
HTTPNetwork httpdequeue(HTTPQueue* h);
void destroyhttpqueue(HTTPQueue* h);

SOCKET inithttpserver(int port,unsigned int maxbuffer,unsigned int maxlisten)
{
	struct sockaddr_in local;
	SOCKET sock;

#if defined(_MSC_VER) || defined(__MINGW32__)
	WSADATA wsaData;
	WSAStartup(0x101, &wsaData);
#endif
	//Now we populate the sockaddr_in structure
	local.sin_family = AF_INET; //Address family
	local.sin_addr.s_addr = INADDR_ANY; //Wild card IP address
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons((u_short)port); //port to use	
	//the socket function creates our SOCKET
	sock = socket(AF_INET, SOCK_STREAM, 0);
	//If the socket() function fails we exit
#if defined(_MSC_VER)    
	if (sock == INVALID_SOCKET) return INVALID_SOCKET;
#else
	if (sock<0) return -1;
#endif
	int enable = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&enable, sizeof(int));
#if defined(__APPLE__)
	setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE , (const char*)&enable, sizeof(int));
#endif
	int msgSize = maxbuffer;
	setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&msgSize, sizeof(msgSize));
	msgSize = maxbuffer;
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&msgSize, sizeof(msgSize));
	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) != 0) return -1;
	listen(sock, maxlisten);			//max connection request at the same time
	return sock;
}

bool inithttpconfig(HTTPNetworkConfig c, int port, unsigned int numproc, unsigned int numqueue, unsigned int maxbuffer, unsigned maxlisten)
{
	c->numprocthread = numproc;
	SOCKET server = inithttpserver(port, maxbuffer, maxlisten);
	if (server <= 0) return false;
	HTTPListenThread t = c->listenthread = (HTTPListenThread)ALLOCMEM(sizeof(struct HTTPListenThreadS));
	if (t == NULL) return false;
	t->port = port;
	t->server = server;
	t->config = c;
	t->lasttime = time(NULL);
	c->pingtimeout = HTTPServerNetwork::PINGTIMEOUT;
	c->maxconnection = numproc;
	c->numprocthread = 0;
#if defined(_PTHREAD)
	pthread_mutex_init(&c->mutex, NULL);
#endif
	if (!inithttpqueue(&c->queue, numqueue))
	{
		FREEMEM(c->listenthread);
		FREEMEM(c);
		return false;
	}
	return true;
}

void destroyhttpconfig(HTTPNetworkConfig c)
{
	HTTPListenThread t = c->listenthread;
	CLOSESOCKET(c->listenthread->server);
	FREEMEM(t);
	destroyhttpqueue(&c->queue);
#if defined(_PTHREAD)
	pthread_mutex_destroy(&c->mutex);
#endif
	FREEMEM(c);
}

HTTPNetwork inithttpnetwork(SOCKET csocket,void* premem,unsigned int size)
{	
	HTTPNetwork n = (HTTPNetwork)ALLOCMEM(sizeof(struct HTTPNetworkS));
	if (n == NULL) return NULL;
	n->buffer=(char*)premem;
	n->bufferIndex = 0;
	n->bufferSize = size;
	n->socket = csocket;
	n->lastping=time(NULL);
	n->sendmsg = NULL;
	n->sendSize = n->sendIndex = 0;
	n->state = HTTPSTATEINIT;
	n->data = NULL;
	return n;
}

void destroyhttpnetwork(HTTPNetwork n)
{
	CLOSESOCKET(n->socket);
	FREEMEM(n->buffer);
	if (n->sendmsg != NULL) FREEMEM(n->sendmsg);
	FREEMEM(n);
}

HTTPProcThread inithttpproc(HTTPNetwork n, HTTPNetworkConfig config)
{
	HTTPProcThread p = (HTTPProcThread)ALLOCMEM(sizeof(struct HTTPProcThreadS));
	if (p == NULL) return NULL;
	p->config = config;
	p->n = n;
	return p;
}

void destroyhttpproc(HTTPProcThread p)
{
	destroyhttpnetwork(p->n);
	FREEMEM(p);
}

int defaulthttpotherfunc(HTTPNetwork msg)
{
	return HTTPMSGEND;
}

int httpsend(HTTPNetwork n,unsigned int pingtimeout)
{
	int r = 0;
	TIME now = time(NULL);
	//no 'dontwait' will block if connection lost 
	int rets = send(n->socket, n->sendmsg, n->sendIndex, MSG_NOSIGNAL | MSG_DONTWAIT); 
	if (rets <= 0)
	{
		if ((unsigned int)(now-n->lastping) < pingtimeout) {
			r = HTTPMSGCONTINUE;
		}
		else {
			n->sendIndex = 0;		//freeze
			r = -1;
		}
	}
	else {
		n->lastping = now;
		if (rets < n->sendIndex) {
			size_t v = n->sendIndex - rets;
			if (rets >= v)
			{
				CPYMEM(n->sendmsg, &n->sendmsg[rets], v);
			} else {			//Double copy
				CPYMEM(n->sendmsg, &n->sendmsg[rets], rets);
				CPYMEM(&n->sendmsg[rets], &n->sendmsg[rets * 2], v - rets);
			}
			n->sendIndex -= rets;
			r = HTTPMSGCONTINUE;
		}
		else {
			n->sendIndex = 0;
			r = HTTPMSGEND;
		}
	}
	return r;
}

void endhttpproc(HTTPProcThread p)
{
	HTTPNetworkConfig config = p->config;
	HTTPNetwork n = p->n;
	if ((n->state != HTTPSTATEINIT) && (n->data!=NULL)) {
		n->state = HTTPSTATEDESTROY;
		config->msgFunc(n);
	}
#if defined(_PTHREAD)
	pthread_mutex_lock(&config->mutex);
#endif
	config->numprocthread--;
#if defined(_PTHREAD)
	pthread_mutex_unlock(&config->mutex);
#endif
	destroyhttpproc(p);
}

/**
* Process thread
* - always destroy network here
* - write socket
*/
void* httpprocthread(void* arg)
{
	fd_set socks;
	struct timeval timeout;
	HTTPProcThread c = (HTTPProcThread)arg;
	int retval,readsocks,r;
	HTTPNetwork n=c->n;
	HTTPNetworkConfig config = c->config;
	HTTPMsgFunc* msgfunc = config->msgFunc;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	r = HTTPMSGCONTINUE;
	while ((r != HTTPMSGEND) && (!config->exitflag) && (time(NULL)-n->lastping<config->pingtimeout)) {
		FD_ZERO(&socks);
		FD_SET(n->socket, &socks);
		readsocks = select(FD_SETSIZE, &socks, (fd_set*)0, (fd_set*)0, &timeout);
		retval = (readsocks>0)?read(n->socket, &n->buffer[n->bufferIndex], n->bufferSize - n->bufferIndex):0;
		if (retval >= 0)		//in case retval==0 or -1
		{
			if (retval > 0) {
				n->bufferIndex += retval;				
				if (n->bufferIndex == n->bufferSize) {			//expand
					if (n->bufferSize >= config->maxmsgsize)	//maximum check
					{
						endhttpproc(c);
						return NULL;
					}
					char* tmp = (char*)ALLOCMEM(n->bufferSize * 2);
					if (tmp != NULL) {
						CPYMEM(tmp, n->buffer, n->bufferSize);
						FREEMEM(n->buffer);
						n->buffer = tmp;
						n->bufferSize = n->bufferSize * 2;
					}
					else {
						endhttpproc(c);
						return NULL;
					}
				}
				n->buffer[n->bufferIndex] = 0;
				n->lastping = time(NULL);
			}
			r = msgfunc(n);
			n->state = HTTPSTATECONTINUE;
			if (n->sendIndex > 0) {
				if (httpsend(n, config->pingtimeout) == -1)
				{
					endhttpproc(c);
					return NULL;
				}
			}
		}
		else if (retval < 0)
		{
			endhttpproc(c);
			return NULL;
		}
	}
	//****************************** write the rest ******************************
	while ((n->sendIndex > 0) && (httpsend(n, config->pingtimeout) == HTTPMSGCONTINUE));
	endhttpproc(c);
	return NULL;
}

/**
* listen thread
*/
void* httplistenthread(void* arg)
{
	HTTPListenThread c = (HTTPListenThread)arg;
	HTTPNetworkConfig config = c->config;
	SOCKET server=c->server;
	HTTPNetwork n;
	SOCKET csocket;
	int readsocks;	
	fd_set socks;
	struct timeval timeout;
	struct sockaddr client;
	int size=sizeof(struct sockaddr);
	//set the timeout to 1 millisec. make the cpu usage too high 
	timeout.tv_sec=0;                    
	timeout.tv_usec=0;					

	//22/06/2023 pre allocate mem for fixing recv bug in Mac OS X
	#define PREMEMSIZE 1000
	void** premem=(void**)ALLOCMEM(PREMEMSIZE*sizeof(void*));
	int prememi=PREMEMSIZE;

	c->lasttime = time(NULL);
	while (!config->exitflag) {
		if (prememi==PREMEMSIZE)
		{
			for (int i=0;i<PREMEMSIZE;i++)
				premem[i]=(void*)ALLOCMEM(config->msgsize);
			prememi=0;
		}
		TIME now = time(NULL);
		//get current tick for process
		unsigned int d=(unsigned int)(now-c->lasttime);		
		if (d>c->tick) c->tick=d; 
		c->lasttime = now;
		//****************** queue ****************************
		if (config->queue.entries>0)
		{
	#if defined(_PTHREAD)
			pthread_mutex_lock(&config->mutex);
	#endif
			if (config->numprocthread < config->maxconnection) {
				HTTPNetwork dn = httpdequeue(&config->queue);
				config->numprocthread++;
	#if defined(_PTHREAD)
				pthread_t workerThreadId;
				pthread_attr_t tattr;
				sched_param schedparam;
				pthread_attr_init(&tattr);
				pthread_attr_getschedparam(&tattr, &schedparam);
				schedparam.sched_priority = 99;
				pthread_attr_setschedparam(&tattr, &schedparam);
				HTTPProcThread p = inithttpproc(dn, config);
				int rc = pthread_create(&workerThreadId, &tattr, httpprocthread, (void*)p);
				if (rc != 0) {
					config->numprocthread--;
					destroyhttpproc(p);
				}
				else {
					pthread_detach(workerThreadId);
				}
	#else
	#endif
			}
	#if defined(_PTHREAD)
			pthread_mutex_unlock(&config->mutex);
	#endif
		} else {
		//****************** accept *****************************
		FD_ZERO(&socks);
		FD_SET(server, &socks);
	#if defined(_MSC_VER) && !defined(__MINGW32__)
	#pragma warning(push, 0)
	#endif
		readsocks=select((server+1), &socks, (fd_set *) 0, (fd_set *) 0, &timeout);	  //select the highest socket
	#if defined(_MSC_VER) && !defined(__MINGW32__)
	#pragma warning( pop )
	#endif
		if (readsocks > 0)
		{
			size = sizeof(struct sockaddr);
		#if defined(_MSC_VER) || defined(__MINGW32__)
			csocket=accept(server,(struct sockaddr *)&client, &size);
		#else
			csocket=accept(server,(struct sockaddr *)&client, (socklen_t*)&size);
		#endif
			if (csocket != -1) {		//11/10/2019 sometimes return -1
					n = inithttpnetwork(csocket,premem[prememi],config->msgsize);	
					prememi++;
					if (n != NULL) {
						n->addr = client;
		#if defined(_PTHREAD)
						pthread_mutex_lock(&config->mutex);
		#endif
						if (config->numprocthread >= config->maxconnection) {
							int i = httpenqueue(&config->queue, n);
							if (i == -1) {
								destroyhttpnetwork(n);
							}
						}
						else {
							config->numprocthread++;
		#if defined(_PTHREAD)
							pthread_t workerThreadId;
							pthread_attr_t tattr;
							sched_param schedparam;
							pthread_attr_init(&tattr);
							pthread_attr_getschedparam(&tattr, &schedparam);
							schedparam.sched_priority = 99;
							pthread_attr_setschedparam(&tattr, &schedparam);
							HTTPProcThread p = inithttpproc(n, config);
							int rc = pthread_create(&workerThreadId, &tattr, httpprocthread, (void*)p);
							if (rc != 0) {
								config->numprocthread--;
								destroyhttpproc(p);
							}
							else {
								pthread_detach(workerThreadId);
							}
		#else
							HTTPProcThread p = inithttpproc(n, config);
							std::thread proc(httpprocthread, (void*)p);
							proc.detach();
		#endif
						}
		#if defined(_PTHREAD)
						pthread_mutex_unlock(&config->mutex);
		#endif
					}
				}
			}
		}
	}
	FREEMEM(premem);
	return NULL;
}

HTTPServerNetwork::HTTPServerNetwork(int port)
{
	config = (HTTPNetworkConfig)ALLOCMEM(sizeof(HTTPNetworkConfigS));
	if (config == NULL) return;
	config->msgFunc = defaulthttpotherfunc;
	config->exitflag = false;
	config->maxconnection = MAXCONNECTION;
	config->pingtimeout = PINGTIMEOUT;
	config->msgsize = MSGSIZE;
	config->maxmsgsize = MAXMSG;
	if (!inithttpconfig(config, port, MAXCONNECTION, MAXQUEUE, MAXBUFFER, MAXLISTEN)) {
		FREEMEM(config);
		config = NULL;
	}
}

void HTTPServerNetwork::setOnMsg(HTTPMsgFunc* p)
{
	if (config == NULL) return;
	config->msgFunc = p;
}

void HTTPServerNetwork::setPath(const char* path)
{
	if (config == NULL) return;
}
 
void HTTPServerNetwork::begin()
{
	if (config == NULL) return;
#if defined(_PTHREAD)
	pthread_t workerThreadId;
	int rc = pthread_create(&workerThreadId, NULL, httplistenthread, (void*)config->listenthread);
	if (rc != 0) return;
	(void)pthread_join(workerThreadId, NULL);
#else
	std::thread workerThread(httplistenthread, (void*)config->listenthread);
	workerThread.join();
#endif
	config->listenthread->config = NULL;
}

void HTTPServerNetwork::exit()
{
	if (config == NULL) return;
	config->exitflag = true;
}

HTTPServerNetwork::~HTTPServerNetwork()
{
	if (config == NULL) return;
	destroyhttpconfig(config);
}

size_t httpurlencode(const char* pSrc,char* ret,size_t rlen)
{	//maybe larger than the source
	const char SAFE[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	const char DEC2HEX[16 + 1] = "0123456789ABCDEF";
	int len = 0;
	char* p = (char*)pSrc;
	while (*p != 0)
	{
		if ((strchr(SAFE, *p) != NULL) || (*p == ' ')) len++;
		else
		{
			len += 3;
		}
		p++;
	}
	if (rlen < len + 1) return 0;
	char* pEnd = ret;
	while (*pSrc != 0)
	{
		if (strchr(SAFE, *pSrc) != NULL) *pEnd++ = *pSrc;
		else if (*pSrc == ' ')
		{
			*pEnd++ = '+';
		}
		else
		{
			// escape this char
			*pEnd++ = '%';
			*pEnd++ = DEC2HEX[(*pSrc >> 4) & 0x0F];
			*pEnd++ = DEC2HEX[*pSrc & 0x0F];
		}
		pSrc++;
	}
	*pEnd = 0;
	return rlen;
}

void httpurldecode(char* pSrc)
{	//may be smaller than the source
	char* pEnd = pSrc;
	while (*pSrc != 0)
	{
		if (*pSrc == '%')
		{
			pSrc++;
			unsigned int dec1 = '?';
			sscanf(pSrc, "%02X", &dec1);
			*pEnd++ = dec1;
			pSrc += 2;
		}
		else if (*pSrc == '+')
		{
			*pEnd++ = ' ';
			pSrc++;
		}
		else
			*pEnd++ = *pSrc++;
	}
	*pEnd = 0;
}

bool httpiscomplete(HTTPNetwork n)
{	//TODO:
	if (n->bufferSize > 2) {
		if (strstr(n->buffer, "POST /") != NULL)
		{   //no end told
			char* p = strstr(n->buffer, "\r\n\r\n");
			if (p == NULL) return false;
			size_t len;
			bool res = httpgetcontentlength(n, &len);
			if (res) {
				size_t l=(p - n->buffer)+4;
				if (n->bufferSize - l >= len) return true;
				return false;
			}
			return true;
		} else {
			//end with \r\n\r\n
			if ((n->buffer[n->bufferSize - 2] == '\r') && (n->buffer[n->bufferSize - 1] == '\n')) {
				n->buffer[n->bufferSize - 2] = 0;
				return true;
			}
		}
	}
	return false;
}

bool httpisheadercomplete(HTTPNetwork n)
{
	if (n->bufferSize > 2) {
		char* p = strstr(n->buffer, "\r\n\r\n");
		if (p == NULL) return false;
		return true;
	}
	return false;
}

int httpgetmethod(HTTPNetwork n)
{
	if (strstr(n->buffer, "GET /") != NULL) return HTTPMETHODGET;
	if (strstr(n->buffer, "POST /") != NULL) return HTTPMETHODPOST;
	if (strstr(n->buffer, "PATCH /") != NULL) return HTTPMETHODPATCH;
	if (strstr(n->buffer, "DELETE /")!=NULL) return HTTPMETHODDELETE;
	return -1;
}

bool httpgetcontentlength(HTTPNetwork n, size_t* len)
{	//Content-Length: length 
	bool found = false;
	char* p = n->buffer;
	while (!found) {
		p = strstr(p, "ength:");
		if ((p == NULL) || (p == n->buffer)) return false;
		char* ph = p; ph--;
		if (tolower(*ph) != 'l') {
			p += 6;
		}
		else {
			ph -= 7;
			if (strstr(ph, "ontent-") != ph) { p += 6; }
			else found = true;
		}
	}
	p += 6;
	if (sscanf(p, "%zu", len) > 0) return true;
	return false;
}

bool httpgetcontenttype(HTTPNetwork n, char* type, char* boundary, size_t len)
{	//Content-Type: multipart/form-data; boundary=------------------------d74496d66958873e
	bool found = false;
	char* p = n->buffer;
	while (!found) {
		p = strstr(p, "ype:");
		if ((p == NULL) || (p == n->buffer)) return false;
		char* ph = p; ph--;
		if (tolower(*ph) != 't') {
			p += 4;
		}
		else {
			ph -= 7;
			if (strstr(ph, "ontent-") != ph) { p += 4; }
			else found = true;
		}
	}
	p += 4;
	while ((*p == ' ') && (*p != 0) && (*p != '\r')) { p++; }
	if (strstr(p, "multipart/form-data;") == p)
	{
		if (len < 20) return false;
		strcpy(type, "multipart/form-data");
		p = strstr(p, "boundary=");
		if (p == NULL) return false;
		char* ep = strstr(p, "\r");
		if (ep == NULL) return false;
		p += 9;
		if (len < ep - p + 1) return false;
		CPYMEM(boundary, p, ep - p);
		boundary[ep - p] = 0;
	}
	else {
		char* ep = strstr(p, "\r");
		if (ep == NULL) return false;
		if (len < ep - p + 1) return false;
		CPYMEM(type, p, ep - p);
		type[ep - p] = 0;
		boundary[0] = 0;
	}
	return true;
}

bool httpgetpath(HTTPNetwork n, char* path,size_t len)
{
	*path=0;
	char *p = strstr(n->buffer, " /");
	if ((p == NULL) || (p-n->buffer>6) || (p-n->buffer<3)) return false;
	p += 2;
	char* p1 = strchr(p, ' '); 
	if (p1 == NULL) return false;
	if (len < p1 - p + 1) return false;
	CPYMEM(path, p, p1 - p);
	path[p1 - p] = 0;
	char* q = strchr(path, '?');
	if (q != NULL) { *q = 0; }
	httpurldecode(path);
	return true;
}

size_t httpgetranges(HTTPNetwork n,size_t** range,size_t total)
{	//Range: bytes=  0-499, -500 (last 500 bytes)
	//0-  infinity
	int numrange = 1;		
	bool found = false;
	char*p = n->buffer;
	while (!found) {
		p = strstr(p, "ange:");
		if ((p == NULL) || (p == n->buffer)) return 0;
		char *ph = p; ph--;
		if (tolower(*ph) != 'r') { p += 5; }
		else found = true;
	}
	p += 5;
	while (*p == ' ') p++;
	if (strstr(p, "bytes=")!=p) return 0;
	p += 6;
	char* e = strstr(p, "\r");
	if (e != NULL) *e = 0;
	char* c = p;
	while (c != e)
	{
		if (*c == ',') numrange++;
		c++;
	}
	*range = (size_t*)ALLOCMEM(sizeof(size_t)*2*numrange);
	if (*range == NULL) return 0;
	c = p;
	int i = 0;
	while ((c != NULL) && (c != e))
	{
		size_t r1, r2;
		r1 = 0;
		r2 = total;
		while (*c == ' ') c++;
		if (c[0] == '-') {	//last bytes
		    int ret=sscanf(c, "-%zu", &r2);
			(*range)[i * 2] = (r2 > total) ? 0: total-r2;
			(*range)[i * 2 + 1] = total - 1;
		}
		else {				//range
			int ret=sscanf(c, "%zu-%zu", &r1, &r2);
			(*range)[i * 2] = (r1 > total - 1) ? total - 1 : r1;
			(*range)[i * 2 + 1] = (r2 > total - 1) ? total - 1 : r2;
		}
		c = strstr(c, ",");
		if (c != NULL) {
			c++;
		}
		i++;
	}
	*e = '\r';
	return numrange;
}

int httpgetnumparam(HTTPNetwork n)
{
	int c = 0;
	char *p = strstr(n->buffer, "GET /");
	if (p == NULL) return 0;
	p += 5;
	char* p1 = strchr(p, ' '); if (p1 == NULL) return 0;
	char* q = strchr(p, '?');
	if ((q == NULL) || (q > p1)) return 0;
	while ((q != NULL) && (q < p1)) {
		q = strchr(q, '=');
		if ((q != NULL) && (q < p1)) c++;
		if (q!=NULL) q++;
	}
	return c;
}

int httppostnumparam(HTTPNetwork n)
{
	int c = 0;
	char* p = strstr(n->buffer, "\r\n\r\n");
	if (p == NULL) return 0;
	p += 4;
	while ((p != NULL) && (*p != 0) && (*p != '\r') && (*p != '\n')) {
		p = strchr(p, '=');
		if (p != NULL) {
			c++;
			p++;
		}
	}
	return c;
}

size_t httpgetparamvalue(HTTPNetwork n, const char* name, char* value,size_t len)
{
	char* p = strstr(n->buffer, "GET /");
	if (p == NULL) return 0;
	p += 5;
	char* p1 = strchr(p, ' '); if (p1 == NULL) return 0;
	char* q = strchr(p, '?');
	if ((q == NULL) || (q > p1)) return 0;
	bool found = false;
	int slen = (int)strlen(name);
	while (!found) {
		q = strstr(q, name);
		if ((q == NULL) || (q > p1)) return 0;
		q += slen;
		if (*q == '=') found = true;
	}
	q++;
	char* ne = strchr(q, '&');
	if ((ne == NULL) || (ne > p1)) ne = p1;
	size_t v = ne - q;
	if (v + 1 > len) return 0;
	CPYMEM(value, q, v);
	value[v] = 0;
	httpurldecode(value);
	return v;
}

bool httpgetparam(HTTPNetwork n, int id, char* name, char* value,size_t len)
{
	char* p = strstr(n->buffer, "GET /");
	if (p == NULL) return false;
	p += 5;
	char* p1 = strchr(p, ' '); if (p1 == NULL) return false;
	char* q = strchr(p, '?');
	if ((q == NULL) || (q > p1)) return false;
	q++;			//remove ?
	for (int i = 0; i < id; i++)
	{
		q = strchr(q, '&');
		if ((q > p1) || (q == NULL)) return false;
	}
	if ((q == NULL) || (q > p1)) return false;
	char* e = strchr(q, '=');
	if (e == NULL) return false;
	size_t v = e - q;
	if (v + 1 > len) return false;
	CPYMEM(name, q, v);
	name[v] = 0;
	e++;			//remove =
	char* ne = strchr(e, '&');
	if ((ne == NULL) || (ne > p1)) ne = p1;
	v = ne - e;
	if (v + 1 > len) return false;
	CPYMEM(value, e, v);
	value[v] = 0;
	httpurldecode(value);
	return true;
}

bool httppostparam(HTTPNetwork n, int id, char* name, char* value,size_t len)
{
	char* p = strstr(n->buffer, "\r\n\r\n");
	if (p == NULL) return false;
	p += 4;
	for (int i = 0; i < id; i++)
	{
		p = strchr(p, '&');
		if (p == NULL) return false;
	}
	if (p == NULL)  return false;
	char* e = strchr(p, '=');
	if (e == NULL) return false;
	size_t v = e - p;
	if (v + 1 > len) return false;
	CPYMEM(name, p, v);
	name[v] = 0;
	e++;			//remove =
	char* ne = strchr(e, '&');
	if (ne == NULL) {
		ne = e;
		ne += strlen(e);
	}
	v = ne - e;
	if (v + 1 > len) return false;
	CPYMEM(value, e, v);
	value[v] = 0;
	httpurldecode(value);
	return true;
}

size_t httppostparamvalue(HTTPNetwork n, const char* name, char* value,size_t len)
{
	char* p = strstr(n->buffer, "\r\n\r\n");
	if (p == NULL) return 0;
	p = strstr(p, name);
	if (p == NULL) return 0;
	p += strlen(name);
	if (*p != '=') return 0;

	p++;
	char* e = p;
	while ((*e != 0) && (*e != '&') && (*e != '\r') && (*e != '\n')) e++;
	size_t v = e - p;
	if (v + 1 > len) return 0;
	CPYMEM(value, p, v);
	value[v] = 0;
	httpurldecode(value);
	return v;
}

bool httpgetuseragent(HTTPNetwork n, char* agent,size_t len)
{   //User-Agent:
	bool found = false;
	char* p = n->buffer;
	while (!found) {
		p = strstr(p, "gent:");
		if ((p == NULL) || (p == n->buffer)) return false;
		char* ph = p; ph--;
		if (tolower(*ph) != 'a') {
			p += 5;
		}
		else {
			ph -= 4;
			if (strstr(ph, "ser-") != ph) { p += 5; }
			else found = true;
		}
	}
	p += 5;
	char* p1 = strchr(p, '\r');
	if (p1 == NULL) return false;
	while ((*p == ':') || (*p == ' ')) p++;
	if (len < p1 - p + 1) return false;
	CPYMEM(agent, p, p1 - p);
	agent[p1 - p] = 0;
	return true;
}

bool httpgethost(HTTPNetwork n, char* host,size_t len)
{	//Host:
	bool found = false;
	char* p = n->buffer;
	while (!found) {
		p = strstr(p, "ost:");
		if ((p == NULL) || (p == n->buffer)) return false;
		char* ph = p; ph--;
		if (tolower(*ph) != 'h') { p += 4; }
		else found = true;
	}
	p += 4;
	char* p1 = strchr(p, '\r');
	if (p1 == NULL) return false;
	while (*p == ' ') p++;
	if (len < p1 - p + 1) return false;
	CPYMEM(host, p, p1 - p);
	host[p1 - p] = 0;
	httpurldecode(host);
	return true;
}

bool httpgetcookie(HTTPNetwork n,const char* name,char* cookie,size_t len)
{	//Cookie: name=value;name2=value2
	char*p = n->buffer;
	bool found = false;
	while (!found) {
		p = strstr(p, "ookie:");
		if ((p == NULL) || (p == n->buffer)) return false;
		char *ph = p; ph--;
		if (tolower(*ph) != 'c') { p += 6; }
		else found = true;
	}
	p += 6;
	while (*p == ' ') p++;
	char* ep = strstr(p, "\r");
	if (ep == NULL) return false;
	p=strstr(p, name);
	if ((p == NULL) || (p > ep)) return false;
	char *rp = p; rp--;
	if (!((*rp == ' ') || (*rp == ';'))) return false;
	p += strlen(name);
	if (*p != '=') return false;
	p++;
	char* ep2 = strstr(p, ";");
	if ((ep2 < ep) && (ep2!=NULL)) ep = ep2;
	if (len < ep - p + 1) return false;
	CPYMEM(cookie, p, (ep - p));
	cookie[ep - p] = 0;
	return true;
}

void httpsettext(HTTPNetwork n, const char* content)
{
	httpsetcontent(n, NULL, "text/plain", (char*)content, strlen(content));
}

void httpsetjson(HTTPNetwork n, const char* content)
{
	httpsetcontent(n, NULL, "application/json", (char*)content, strlen(content));
}

void httpsetjson(HTTPNetwork n, unsigned int code, const char* content)
{
	httpsetcontentwithcode(n, code,NULL, "application/json", (char*)content, strlen(content));
}

void httpsetcontent(HTTPNetwork n, const char* header, const char* contenttype, char* content, size_t total)
{	//main response
	size_t len = strlen(content) + ((header != NULL) ? strlen(header) : 0);
	if (len + 256 > n->sendSize) {
		if (n->sendmsg != NULL) FREEMEM(n->sendmsg);
		n->sendmsg = (char*)ALLOCMEM(len + 256);
		n->sendSize = (unsigned int)len + 256;
	}
	if (n->sendmsg != NULL) {
		if (header == NULL)
			sprintf(n->sendmsg, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %zu\r\nContent-Type: %s\r\n\r\n", total, contenttype);
		else 
			sprintf(n->sendmsg, "HTTP/1.1 200 OK\r\n%s\r\nConnection: close\r\nContent-Length: %zu\r\nContent-Type: %s\r\n\r\n", header, total, contenttype);
		size_t l = strlen(n->sendmsg);
		CPYMEM(&n->sendmsg[l], content, len);
		n->sendIndex = (l + len);
	}
}

void httpsetcontentwithcode(HTTPNetwork n, unsigned int code, const char* header, const char* contenttype,char* content, size_t total)
{	//main response with code
	if (code == 200) { httpsetcontent(n, header, contenttype, content, total); return; }
	size_t len = strlen(content) + ((header != NULL) ? strlen(header) : 0);
	if (len + 256 > n->sendSize) {
		if (n->sendmsg != NULL) FREEMEM(n->sendmsg);
		n->sendmsg = (char*)ALLOCMEM(len + 256);
		n->sendSize = (unsigned int)len + 256;
	}
	if (n->sendmsg != NULL) {
		char status[512];
		httpcode(code, status);
		if (header == NULL)
			sprintf(n->sendmsg, "HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Length: %zu\r\nContent-Type: %s\r\n\r\n",code, status,total, contenttype);
		else
			sprintf(n->sendmsg, "HTTP/1.1 %d %s\r\n%s\r\nConnection: close\r\nContent-Length: %zu\r\nContent-Type: %s\r\n\r\n", code, status, header, total, contenttype);
		size_t l = strlen(n->sendmsg);
		CPYMEM(&n->sendmsg[l], content, len);
		n->sendIndex = (l + len);
	}
}

void httpset(HTTPNetwork n, size_t total)
{
	if (n->sendmsg != NULL) FREEMEM(n->sendmsg);
	n->sendmsg = (char*)ALLOCMEM(total+2000);	//padding buffer for boundary
	n->sendSize = (unsigned int)total+1;
	n->sendIndex = 0;
}

void httpsetraw(HTTPNetwork n, const char* header,const char* contenttype, size_t total)
{
	if (n->sendSize - n->sendIndex < 256) return;
	if (header == NULL)
		sprintf(n->sendmsg, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %zu\r\nContent-Type: %s\r\n\r\n", total, contenttype);
	else 
		sprintf(n->sendmsg, "HTTP/1.1 200 OK\r\n%s\r\nConnection: close\r\nContent-Length: %zu\r\nContent-Type: %s\r\n\r\n", header, total, contenttype);
	n->sendIndex = strlen(n->sendmsg);
}

void httpsetacceptranges(HTTPNetwork n, const char* header, const char* contenttype,size_t total)
{
	if (n->sendSize - n->sendIndex < 256) return;
	if (header == NULL)
		sprintf(n->sendmsg, "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nContent-Length: %zu\r\nContent-Type: %s\r\n\r\n", total, contenttype);
	else 
		sprintf(n->sendmsg, "HTTP/1.1 200 OK\r\n%s\r\nAccept-Ranges: bytes\r\nContent-Length: %zu\r\nContent-Type: %s\r\n\r\n", header, total, contenttype);
	n->sendIndex = strlen(n->sendmsg);
}

void httpsetsingleranges(HTTPNetwork n, const char* header, const char* contenttype, size_t size,size_t rangea,size_t rangeb)
{	//Content-Duration: <sec>
	if (n->sendSize - n->sendIndex < 256) return;
	if (header == NULL)
		sprintf(n->sendmsg, "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\nContent-Range: bytes %zu-%zu/%zu\r\nContent-Length: %zu\r\nContent-Type: %s\r\n\r\n", rangea, rangeb, size, (rangeb-rangea+1), contenttype);
	else 
		sprintf(n->sendmsg, "HTTP/1.1 206 Partial Content\r\n%s\r\nAccept-Ranges: bytes\r\nContent-Range: bytes %zu-%zu/%zu\r\nContent-Length: %zu\r\nContent-Type: %s\r\n\r\n", header,rangea, rangeb, size, (rangeb - rangea + 1), contenttype);
	n->sendIndex = strlen(n->sendmsg);
}

void httpsetmultiranges(HTTPNetwork n, const char* header,const char* contenttype, size_t size,int numrange, size_t* range,const char* boundary)
{	
	if (n->sendSize - n->sendIndex < 256*(numrange+1)) return;
	size_t len = 0;
	n->sendIndex = 0;
	for (int i = 0; i < numrange; i++)
	{
		len += range[i * 2 + 1] - range[i * 2]+1;
		httpbeginboundarymultiranges(n, contenttype, boundary, size, range[i*2], range[i*2+1]);
		httpendboundarymultiranges(n, boundary);
	}
	len += n->sendIndex;
	if (header==NULL)
		sprintf(n->sendmsg, "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\nContent-Type: multipart/byteranges; boundary=%s\r\nContent-Length: %zu\r\n\r\n", boundary, len);
	else
		sprintf(n->sendmsg, "HTTP/1.1 206 Partial Content\r\n%s\r\nAccept-Ranges: bytes\r\nContent-Type: multipart/byteranges; boundary=%s\r\nContent-Length: %zu\r\n\r\n",header,boundary,len);
	n->sendIndex = strlen(n->sendmsg);
}

void httpbeginboundarymultiranges(HTTPNetwork n,const char* contenttype, const char* boundary, size_t size, size_t rangea, size_t rangeb)
{	//use padding buffer
	sprintf(&n->sendmsg[n->sendIndex], "--%s\nContent-Type: %s\r\nContent-Range: bytes %zu-%zu/%zu\r\n\r\n", boundary, contenttype, rangea, rangeb, size);
	n->sendIndex += strlen(&n->sendmsg[n->sendIndex]);
}

void httpendboundarymultiranges(HTTPNetwork n,const char* boundary)
{	//use padding buffer 
	sprintf(&n->sendmsg[n->sendIndex], "--%s\r\n", boundary);
	n->sendIndex += strlen(&n->sendmsg[n->sendIndex]);
}

void httpendfooter(HTTPNetwork n)
{
	if (n->sendSize - n->sendIndex < 6) return;
	sprintf(&n->sendmsg[n->sendIndex], "\r\n\x0D\x0D");
	n->sendIndex += 4;
}

void httplastmodified(TIME lastmodified, char* header)
{
	char r[512];
	const char* dayname[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
	const char* monthname[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
	tm* tmp = localtime(&lastmodified);

	sprintf(r, "%s, %02d %s %04d %02d:%02d:%02d GMT", dayname[tmp->tm_wday], tmp->tm_mday, monthname[tmp->tm_mon], tmp->tm_year + 1900, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
	sprintf(header, "Last-Modified: %s", r);
}

void httpsetcookie(const char* name, const char* value,char* header)
{	//Set-Cookie: name=value
	//Set-Cookie: name2=value2; Expires = Wed, 09 Jun 2021 10:18 : 14 GMT
	size_t len = strlen(value);
	*header = 0;
	char* newvalue = (char*)ALLOCMEM(len*4+1);
	if (newvalue != NULL) {
		*newvalue = 0;
		httpurlencode(value, newvalue, len * 4 + 1);
		sprintf(header, "Set-Cookie: %s=%s", name, newvalue);
		FREEMEM(newvalue);
	}
}

bool httpcode(unsigned int code,char* status)
{
	switch (code)
	{
		case 400: { strcpy(status, "Bad Request");  return true;  }
		case 401: { strcpy(status, "Unauthorized");  return true;  }
		case 404: { strcpy(status, "Not Found");  return true;  }
		case 500: { strcpy(status, "Internal Server Error");  return true;  }
		case 501: { strcpy(status, "Not Implemented");  return true;  }
	}
	return false;
}

bool httpcontenttype(const char* filename, char* contenttype)
{
	if (filename == NULL)
	{
		strcpy(contenttype, "application/octet-stream");
		return true;
	}
	char* ext = strstr((char*)filename, ".");
	char* p = ext;
	while (p != NULL) {
		ext++;
		p = strstr(ext, ".");
		if (p != NULL) {
			p++;
			ext = p;
		}
	}
	if (ext == NULL) {
		strcpy(contenttype, "application/octet-stream");
	}
	else if (strcmp(ext, "html") == 0)
		strcpy(contenttype, "text/html");
	else if (strcmp(ext, "json") == 0)
		strcpy(contenttype, "application/json");
	else if (strcmp(ext, "js") == 0)
		strcpy(contenttype, "text/javascript");
	else if ((strcmp(ext, "jpg") == 0) || (strcmp(ext, "jpeg") == 0))
		strcpy(contenttype, "image/jpeg");
	else if (strcmp(ext, "png") == 0)
		strcpy(contenttype, "image/png");
	else if (strcmp(ext, "css") == 0)
		strcpy(contenttype, "text/css");
	else if (strcmp(ext, "mp3") == 0)
		strcpy(contenttype, "audio/mpeg");
	else if (strcmp(ext, "mp4") == 0)
		strcpy(contenttype, "video/mp4");
	else if (strcmp(ext, "3gp") == 0)
		strcpy(contenttype, "video/3gpp");
	else if (strcmp(ext, "mov") == 0)
		strcpy(contenttype, "video/quicktime");
	else if (strcmp(ext, "avi") == 0)
		strcpy(contenttype, "video/x-msvideo");
	else if (strcmp(ext, "mwv") == 0)
		strcpy(contenttype, "video/x-ms-wmv");
	else if (strcmp(ext, "zip") == 0)
		strcpy(contenttype, "application/zip");
	else if (strcmp(ext, "txt") == 0)
		strcpy(contenttype, "text/plain");
	else {
		strcpy(contenttype, "application/octet-stream");
	}
	return true;
}

unsigned int httpqueuenearestpoweroftwo(unsigned int x)
{
	unsigned int i;
	unsigned int j = 2;
	for (i = 0; i < 30; i++)
	{
		if (j >= x) return j;
		j = j << 1;
	}
	return 0;
}

bool inithttpqueue(HTTPQueue* p,unsigned int size)
{
	p->size = httpqueuenearestpoweroftwo(size);
	p->entries = 0;
	p->first = 0;
	p->last = 0;
	p->mask = p->size - 1;
	p->data = (HTTPNetwork*)ALLOCMEM(sizeof(HTTPNetwork) * p->size);
	if (p->data == NULL) { FREEMEM(p); return false; }
	return true;
}

int httpenqueue(HTTPQueue* h, HTTPNetwork value)
{
	int i;
	if (h->entries != h->size)
	{
		h->data[h->last] = value;
		i = h->last;
		h->last = (h->last + 1) & h->mask;
		h->entries++;
		return i;
	}
	return -1;
}

HTTPNetwork httpdequeue(HTTPQueue* h)
{
	if (h->entries == 0) return NULL;
	HTTPNetwork v = h->data[h->first];
	h->first = (h->first + 1) & h->mask;
	h->entries--;
	return v;
}

void destroyhttpqueue(HTTPQueue* h)
{
	FREEMEM(h->data);
	FREEMEM(h);
}
