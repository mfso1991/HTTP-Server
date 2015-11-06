#include "minet_socket.h"
#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netdb.h>


#define BUFSIZE 1024

int main(int argc, char * argv[]) {

    char * server_name = NULL;
    int server_port    = -1;
    char * server_path = NULL;
    char * req         = NULL;
    bool ok            = false;
    
    struct hostent *hp;
    struct sockaddr_in saddr;
    int sock_fd, res, flag = 0;
    fd_set set;
    char buf[BUFSIZE];

    /*parse args */
    if (argc != 5) {
	fprintf(stderr, "usage: http_client k|u server port path\n");
	exit(-1);
    }

    server_name = argv[2];
    server_port = atoi(argv[3]);
    server_path = argv[4];
   
 
   /* initialize */
    if (toupper(*(argv[1])) == 'K') { 
	 minet_init(MINET_KERNEL); //minet socket initialized using kernel stack
    } else if (toupper(*(argv[1])) == 'U') { 
	 minet_init(MINET_USER); //minet socket initialized using user stack  
    } else {
	fprintf(stderr, "First argument must be k or u\n");
	exit(-1);
    }

    /* make socket */
	if( (sock_fd = minet_socket(SOCK_STREAM)) < 0 ){
		minet_perror("client : socket");
        minet_deinit();
        exit(-1);
	}

    /* get host IP address  */
    /* Hint: use gethostbyname() */
	if((hp = gethostbyname(server_name)) == NULL){
	    minet_perror("client : IP address");
        minet_close(sock_fd);
        minet_deinit();
	    exit(-1);
	}
   
    /* set address */
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	memcpy(&saddr.sin_addr.s_addr, hp->h_addr, hp->h_length);
	saddr.sin_port = htons(server_port);

    /* connect to the server socket */
	if((minet_connect(sock_fd, (struct sockaddr_in *)&saddr))<0){
	//error processing if fail to connect 
        minet_perror("client : connect");
        minet_close(sock_fd);
        minet_deinit();
        exit(-1);
	}

    /* send request message */
    //do we need to consider the situation that server_path start with a /？
    req = (char *)malloc(strlen("GET  HTTP/1.0\r\n\r\n") + strlen(server_path) + 1);
    //if(server_path[0]=='/'){
    //sprintf(req, "GET /%s HTTP/1.0\r\n\r\n", server_path);
    //}
   //else 
    sprintf(req, "GET %s HTTP/1.0\r\n\r\n", server_path);
    
   if((minet_write(sock_fd, req, strlen(req)))<0){
        minet_perror("client : write");
        minet_close(sock_fd);
        minet_deinit();
        free(req);
        exit(-1);
    }

    /* wait till socket can be read. */
    /* Hint: use select(), and ignore timeout for now. */
    FD_CLR(sock_fd, &set);
    FD_SET(sock_fd, &set);
    if((minet_select(sock_fd+1, &set, NULL, NULL, NULL))<0){
        perror("client : select");
        minet_close(sock_fd);
        minet_deinit();
        free(req);
        exit(-1);
    }

     /* first read loop -- read headers */
    std::string header;
    std::string response; 
    std::size_t found;
    memset(buf, 0, BUFSIZE);
    while((res = minet_read(sock_fd, buf, BUFSIZE))>0){
        //buf[res] = '\0';
        std::string content(buf); //copy into
        response += content;

        //carriage return and line feed indicating eof of headers, splite headers and response
        //found records the position (length) 
        if((found = response.find("\r\n\r\n")) != std::string::npos){
            header = response.substr(0, found); //header - everything before
            response = response.substr(found);
            flag = 1; // indicate that read succeed
            break; 
        }
    }  
    //printf("%s", buf); 

    if(flag != 1){
        perror("client : read");
        minet_close(sock_fd);
        minet_deinit();
        free(req);
        exit(-1);
    }

    /* examine return code */
    //Skip "HTTP/1.0"
    //remove the '\0' (what does it mean? )
    // Normal reply has return code 200
    if((header.substr(header.find(" ") + 1, 3)) == "200"){
        ok = true; //ok
    }
    printf("%s\n", (header.substr(header.find(" ")+1, 3)).c_str());

    /* print first part of response: header, error code, etc. */
    printf("%s", header.c_str());
    printf("%s", response.c_str()); 
    //clean up buf array 
   
   // second read loop -- print out the rest of the response: real web content 
    //res = minet_read(sock_fd, buf, BUFSIZE - 1);
    if(ok){
        printf("%s\n", "ok");
    }
    if(ok){
    //printf("%s", response.c_str()); 
    while(true){
        memset(buf, 0, BUFSIZE);
        res = minet_read(sock_fd, buf, BUFSIZE);
        if(res == 0)
            break;
        else if(res < 0){
            perror("client: data");
            minet_close(sock_fd);
            minet_deinit();
            free(req);
            exit(-1);
        }
        printf("%s", buf);
    }
    }
   // minet_read(sock_fd, buf, BUFSIZE);
    //printf("%s", buf);

    /*close socket and deinitialize */
	minet_close(sock_fd);
	minet_deinit();
    free(req);

    if (ok) {
	return 0;
    } else {
	return -1;
    }
}
