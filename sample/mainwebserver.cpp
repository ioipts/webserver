#include "axishttpsock.h"

struct HTTPFile
{
	FILE* f;
	size_t total;
	size_t seek;
	size_t* range;
	int numRange;
	char contenttype[512];
} PACKED;

#define BOUNDARY "abc"
#define SENDBUFFERSIZE 1000000
#define HEADER NULL

HTTPServerNetwork* http;
int port = 8027;
char webdir[1024];

int httpmsg(HTTPNetwork msg)
{	
	switch (msg->state)
	{
		case HTTPSTATEINIT: {

 		}
		case HTTPSTATECONTINUE: {
			HTTPFile* f = (HTTPFile*)msg->data;
			if ((f==NULL) && (httpisheadercomplete(msg))) {
				char path[1005];
				if (httpgetpath(msg, path, 1000) > 0)
				{
					char file[1024];
					sprintf(file, "%s%s%s", webdir,SEPERATORSTR,path);
					FILE* fo = FOPEN(file, "rb");
					if (fo == NULL)
					{
						httpsetjson(msg, 404, "");
						return HTTPMSGEND;
					}
					msg->data = (void*)ALLOCMEM(sizeof(HTTPFile));
					if (msg->data == NULL) return HTTPMSGEND;
					f = (HTTPFile*)msg->data;
					f->f = fo;
					f->numRange = -1;
					f->range = NULL;
					f->seek = 0;
					f->total = 0;
					httpcontenttype(path, f->contenttype);
					FSEEK(f->f, 0, SEEK_END);
					f->total = FTELL(f->f);
#if defined(X32) 
					if (f->total == 0xFFFFFFFF) {
						httpsetjson(msg, 500, "");
						return HTTPMSGEND;
					}
#endif
				}
				f->numRange = (int)httpgetranges(msg, &f->range, f->total);
				httpset(msg,SENDBUFFERSIZE);			
				if (f->numRange==0) {
					httpsetacceptranges(msg,HEADER, f->contenttype, f->total);
					FSEEK(f->f, 0, SEEK_SET);
				} else 
				if (f->numRange == 1) {
					httpsetsingleranges(msg, HEADER, f->contenttype, f->total,f->range[0],f->range[1]);
					f->seek = f->range[0];
					FSEEK(f->f, f->range[0], SEEK_SET);
				} else 
				if (f->numRange > 1) {
					httpsetmultiranges(msg, HEADER, f->contenttype, f->total, f->numRange, f->range,BOUNDARY);
					httpbeginboundarymultiranges(msg, f->contenttype, BOUNDARY, f->total, f->range[0], f->range[1]);
					f->seek = f->range[0];
					FSEEK(f->f, f->range[0], SEEK_SET);
				}
			}
			if (f != NULL) {
				if (f->numRange <= 1) {
					size_t last = ((f->numRange == 0) || (f->range == NULL)) ? f->total - 1 : f->range[1];
					if ((f->seek <= last) && (msg->sendIndex < msg->sendSize)) {
						size_t v = (msg->sendSize - msg->sendIndex < last - f->seek + 1) ? msg->sendSize - msg->sendIndex : last - f->seek + 1;
						FREAD(&msg->sendmsg[msg->sendIndex], 1, v, f->f);
						f->seek += v;
						msg->sendIndex += v;
					}
					if (f->seek > last) return HTTPMSGEND;
				}
				else if (f->numRange > 1) {
					int rindex = -1;				//find index
					size_t last = 0;
					for (int i = 0; i < f->numRange; i++)
					{
						if ((f->seek >= f->range[i * 2]) && (f->seek <= f->range[i * 2 + 1])) { rindex = i; last = f->range[i * 2 + 1];  break; }
					}
					if (rindex == -1) return HTTPMSGEND;
					if ((f->seek <= last) && (msg->sendIndex < msg->sendSize)) {
						size_t v = (msg->sendSize - msg->sendIndex < last - f->seek + 1) ? msg->sendSize - msg->sendIndex : last - f->seek + 1;
						FREAD(&msg->sendmsg[msg->sendIndex], 1, v, f->f);
						f->seek += v;
						msg->sendIndex += v;
					}
					if (f->seek > last) {	//shift next
						httpendboundarymultiranges(msg, BOUNDARY);
						if (rindex >= f->numRange) return HTTPMSGEND;
						rindex++;
						httpbeginboundarymultiranges(msg, f->contenttype, BOUNDARY, f->total, f->range[rindex * 2], f->range[rindex * 2 + 1]);
						f->seek = f->range[rindex * 2];
						FSEEK(f->f, f->range[rindex * 2], SEEK_SET);
					}
				}
			}
			return HTTPMSGCONTINUE;
		} break;
		case HTTPSTATEDESTROY: {
			HTTPFile* fo = (HTTPFile*)msg->data;
			if (fo->range != NULL) FREEMEM(fo->range);
			if (fo->f != NULL) FCLOSE(fo->f);
			FREEMEM(msg->data);
		} break;
	}
	return HTTPMSGEND;
}

#if defined(_WINDOWS)
#include "windows.h"
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int argc,char** argv)
#endif
{	
	strcpy(webdir, ".");
	if (argc >= 2) {
		sscanf(argv[1], "%d", &port);
	}
	if (argc >= 3) {
		sscanf(argv[2], "%s", webdir);
	}
	http=new HTTPServerNetwork(port);
	http->setPath(webdir);
	http->setOnMsg(httpmsg);
	printf("Web server 2.0:\n");
	http->begin();
	delete http;
	return 0;
}


