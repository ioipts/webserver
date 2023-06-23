#include "axishttpsock.h"

struct HTTPDownload
{
	int type;
	FILE* f;
	size_t total;
	size_t seek;
	size_t* range;
	int numRange;
	char contenttype[512];
} PACKED;

struct HTTPUpload
{
	int type;
	FILE* f;
	int state;
	size_t boundarylen;
	char boundary[512];
} PACKED;

#define HTTP_TYPE_UPLOAD 1
#define HTTP_TYPE_DOWNLOAD 2
#define HTTP_UPLOAD_INIT 0
#define HTTP_UPLOAD_CONTINUE 1

#define BOUNDARY "axis"
#define SENDBUFFERSIZE 1000000
#define DEFAULTPATH "."
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
			HTTPDownload* f = (HTTPDownload*)msg->data;
			//get header
			if ((f == NULL) && (httpisheadercomplete(msg))) {
				char path[1005];					
				char contenttype[512];
				char boundary[512];
				boundary[0] = 0;
				int method = 0;
				bool acceptfile = false;
				if (!httpgetpath(msg, path, 1000)) return HTTPMSGEND;
				method = httpgetmethod(msg);
				if ((strstr(path, ".") != 0) && (method == HTTPMETHODGET)) {
					char file[2024];
					sprintf(file, "%s%s%s", webdir, SEPERATORSTR, path);
					FILE* fo = FOPEN(file, "rb");
					if (fo == NULL)
					{
						httpsetjson(msg, 404, "");
						return HTTPMSGEND;
					}
					msg->data = (void*)ALLOCMEM(sizeof(HTTPDownload));
					if (msg->data == NULL) return HTTPMSGEND;
					f = (HTTPDownload*)msg->data;
					f->type = HTTP_TYPE_DOWNLOAD;
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
					f->numRange = (int)httpgetranges(msg, &f->range, f->total);
					httpset(msg, SENDBUFFERSIZE);
					if (f->numRange == 0) {
						httpsetacceptranges(msg, HEADER, f->contenttype, f->total);
						FSEEK(f->f, 0, SEEK_SET);
					}
					else if (f->numRange == 1) {
						httpsetsingleranges(msg, HEADER, f->contenttype, f->total, f->range[0], f->range[1]);
						f->seek = f->range[0];
						FSEEK(f->f, f->range[0], SEEK_SET);
					}
					else if (f->numRange > 1) {
						httpsetmultiranges(msg, HEADER, f->contenttype, f->total, f->numRange, f->range, BOUNDARY);
						httpbeginboundarymultiranges(msg, f->contenttype, BOUNDARY, f->total, f->range[0], f->range[1]);
						f->seek = f->range[0];
						FSEEK(f->f, f->range[0], SEEK_SET);
					}
					acceptfile = true;
				}
				else if (method==HTTPMETHODPOST) {
					if (!httpgetcontenttype(msg, contenttype, boundary, 512)) return HTTPMSGEND;
					if ((strcmp(contenttype, "multipart/form-data") == 0) && (strlen(boundary)>0)) {
						msg->data = (void*)ALLOCMEM(sizeof(HTTPUpload));
						if (msg->data == NULL) return HTTPMSGEND;
						f = (HTTPDownload*)msg->data;
						f->type = HTTP_TYPE_UPLOAD;
						HTTPUpload* u= (HTTPUpload*)msg->data;
						u->f = NULL;
						u->state = HTTP_UPLOAD_INIT;
						sprintf(u->boundary, "%s", boundary);
						u->boundarylen = strlen(boundary);
						char* headerend=strstr(msg->buffer, "\r\n\r\n");
						if (headerend == NULL) return HTTPMSGEND;
						//shift
						httpshiftbuffer(msg, headerend - msg->buffer + 4);
						httpexpandbuffer(msg, SENDBUFFERSIZE);		
						acceptfile = true;
					}
				}
				if (!acceptfile) {
					//TODO: implement other endpoints here
					httpsetjson(msg, "{\"status\":\"ok\"}");
					return HTTPMSGEND;
				}
			}
			//upload 
			if ((f != NULL) && (f->type == HTTP_TYPE_UPLOAD)) {
				HTTPUpload* u = (HTTPUpload*)msg->data;
				if (u->state == HTTP_UPLOAD_INIT) {
					char* check=NULL;
					if (httpisheadercomplete(msg)) {						
						char* headerend=strstr(msg->buffer,"\r\n\r\n");
						if (headerend == NULL) return HTTPMSGEND;
						*headerend = 0;
						char file[2024];
						char path[2024];
						u->f = NULL;
						if (httpgetfilename(msg->buffer, file, 2024)) {
							sprintf(path, "%s%s%s", webdir, SEPERATORSTR, file);
							u->f = FOPEN(path, "wb");
						}
						httpshiftbuffer(msg, headerend - msg->buffer + 4);
						u->state = HTTP_UPLOAD_CONTINUE;
					} else if ((check = strstr(msg->buffer, "--\r\n"))!=NULL) {					
						//TODO: finish upload
						httpsetjson(msg, "{\"status\":\"success\"}");
						return HTTPMSGEND;
					}
				}
				if (u->state == HTTP_UPLOAD_CONTINUE) {
					bool found = false;
					char* b = msg->buffer;
					size_t left = msg->bufferIndex;
					while ((!found) && (left>=u->boundarylen+2)) {
						char* c = (char*)memchr(b, '-', left);
						if (c==NULL) { left = 0; }
						else if (*(c + 1) != '-') { b = c + 1; left = msg->bufferIndex - (c + 1 - msg->buffer); }
						else if (left - (c - msg->buffer + 2) < u->boundarylen) { b = c; left = msg->bufferIndex - (c - msg->buffer); }
						else if (memcmp(c+2,u->boundary,u->boundarylen)==0) { found = true; left = msg->bufferIndex-(c - msg->buffer); }
						else { b = c + 2; left = msg->bufferIndex - (c + 2 - msg->buffer); }
					}
					size_t clen = msg->bufferIndex-left;
					if (clen > 0) {
						if (u->f != NULL) {
							FWRITE(msg->buffer, 1, clen, u->f);
						}
						httpshiftbuffer(msg, clen);
					}
					if (found) {
						u->state = HTTP_UPLOAD_INIT;
						if (u->f != NULL) {
							FFLUSH(u->f);
							FCLOSE(u->f);
							u->f = NULL;
						}
					}
				}
			}
			//download
			if ((f != NULL) && (f->type==HTTP_TYPE_DOWNLOAD)) {
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
			HTTPDownload* fo = (HTTPDownload*)msg->data;
			if (fo->type == HTTP_TYPE_DOWNLOAD) {
				if (fo->range != NULL) FREEMEM(fo->range);
				if (fo->f != NULL) FCLOSE(fo->f);
			}
			else {
				HTTPUpload* fu = (HTTPUpload*)msg->data;
				if (fu->f != NULL) {
					FFLUSH(fu->f);  
					FCLOSE(fu->f);
				}
			}
			FREEMEM(msg->data);
		} break;
	}
	return HTTPMSGEND;
}

#if defined(_WINDOWS)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int argc,char** argv)
#endif
{	
	strcpy(webdir, DEFAULTPATH);
	if (argc >= 2) {
		sscanf(argv[1], "%d", &port);
	}
	if (argc >= 3) {
		sscanf(argv[2], "%s", webdir);
	}
	http=new HTTPServerNetwork(port);
	http->setPath(webdir);
	http->setOnMsg(httpmsg);
	printf("Axis Web server:\n");
	http->begin();
	delete http;
	return 0;
}
