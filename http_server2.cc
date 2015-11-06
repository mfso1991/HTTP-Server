
#include "minet_socket.h"
#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netdb.h>


#define BUFSIZE 1024
#define FILENAMESIZE 100
#define BACKLOG 11 // one is for server socket, and ten are for connecting client-side sockets. 

int handle_connection(int sock)
{
    bool ok = false;
    bool subfound = false;
    char *buf = (char *)malloc(BUFSIZE);
    int res, content_length;
    
    const char * ok_response_f = "HTTP/1.0 200 OK\r\n"  \
    "Content-type: text/plain\r\n"          \
    "Content-length: %d \r\n\r\n";
    
    const char * notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n" \
    "Content-type: text/html\r\n\r\n"           \
    "<html><body bgColor=black text=white>\n"       \
    "<h2>404 FILE NOT FOUND</h2>\n"
    "</body></html>\n";
    /* first read loop -- get request and headers*/
    
    std::string request;
    std::size_t found;
    int flag = 0;
    while((res = minet_read(sock, buf, BUFSIZE))>0){
        std::string content(buf); //copy into
        request +=content;
        memset(buf, 0 ,BUFSIZE);
        
        //carriage return and line feed indicating eof of headers, splite headers and response
        //found records the position (length)
        if((found = request.find("\r\n\r\n")) != std::string::npos){
            request = request.substr(0, found); //header - everything before
            flag = 1; // indicate that read succeed
            break;
        }
    }
    
    if(flag == 0)
    {
        minet_perror("server : fail to read request");
        //return -2;
    }
    else
    {
        /* parse request to get file name */
        /* Assumption: this is a GET request and filename contains no spaces*/
        //GET filename HTTP
        //found, length of string before http, including 2 spaces and 1 get
        found = request.find("HTTP");
        if(found>9)
        {
            subfound = true;
            std::string file_string (request.substr(4, found -5));
            char file_name[FILENAMESIZE];
            strncpy(file_name, file_string.c_str(), sizeof(file_name));
            
            // try opening the file
            FILE *fp = fopen(file_name, "r");
            if(fp != NULL)
            {
                ok = true;
            }
            
            //send response
            if(ok)
            {
                // send headers
                fseek(fp, 0, SEEK_END); //put pointer to eof
                content_length = ftell(fp); //know the position of file pointer
                
                char ok_response [strlen(ok_response_f)+1]; //store ok_response
                sprintf(ok_response, ok_response_f, content_length); //format ok response
                if((minet_write(sock, ok_response, strlen(ok_response)))<0)
                {
                    minet_perror("server : response + headers");
                    ok = false;
                }
                
                memset(buf, 0 , sizeof(buf));
                rewind(fp); //put back to beginning
                
                while((fread(buf, sizeof(char), BUFSIZE, fp))>0)
                {
                    if((minet_write(sock, buf, BUFSIZE))<0)
                    {
                        minet_perror("server : data");
                        ok = false;
                        break;
                    }
                    memset(buf, 0 , BUFSIZE); //clear buf set
                }
                fclose(fp);
            }
            else
            {
                // send error response
                ok = true;
                if((minet_write(sock, (char *)notok_response, strlen(notok_response)))<0)
                {
                    minet_perror("server : response + headers");
                    ok = false;
                }
            }
        }
    }
    /* close socket and free space */
    minet_close(sock);
    free(buf);
    
    if(!subfound)
    {
        return -2;
    }
    else
    {
        if (ok)
            return 0;
        else
            return -1;
    }
}

int main(int argc, char * argv[]) 
{
    // checking args' counts
    if (argc != 3) 
    {
        fprintf(stderr, "usage: http_server1 k|u port\n");
        exit(-1);
    }

    // checking port num range
    int server_port = atoi(argv[2]);
    if (server_port < 1500) 
    {
        fprintf(stderr, "INVALID PORT NUMBER: %d; can't be < 1500\n", server_port);
        exit(-1);
    }
    
    // minet initialization
    if (toupper(*(argv[1])) == 'K')  
        minet_init(MINET_KERNEL);   // kernel stack
    else if (toupper(*(argv[1])) == 'U')  
        minet_init(MINET_USER);     // user stack
	else
    {
        fprintf(stderr, "First argument must be k or u\n");
        exit(-1);
    }

    // creating socket 
    int server_sock = -1;
    if((server_sock = minet_socket(SOCK_STREAM)) < 0)
    {
        fprintf(stderr, "ERROR CREATING SOCKET.\n");
        exit(-1);
    }  

	// loading server address info
    struct sockaddr_in server_addr;
	memset(&server_addr , 0, sizeof(struct sockaddr_in));
	server_addr.sin_family = AF_INET ;  
	server_addr.sin_port = htons(atoi(argv[2]));
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY) ;
	
	// binding the init_sock
    if(minet_bind(server_sock, (struct sockaddr_in *)&server_addr) < 0) 
    {
        fprintf(stderr, "ERROR BINDING SOCKET.\n");
        exit(-1);
    }
	
	// listening to at most BACKLOG connections
	if(minet_listen(server_sock , BACKLOG) < 0) 
    {
		fprintf(stderr, "ERROR LISTENING SOCKET!\n");
		exit(-1);
	}
    
    // initializing message queue to all zero
    fd_set readfds, tempfds;
    FD_ZERO(&readfds);
    FD_SET(server_sock, &readfds);
    int maxfd = server_sock;
    int rc;
    while (1) 
    {
        tempfds = readfds;  // select will change the state of fd_set
        // start selecting over the sockets
        if(minet_select(maxfd + 1, &tempfds, NULL, NULL, NULL) < 0) // ignoring time out
        {
            fprintf(stderr, "ERROR SELECTING SOCKET!\n");
            exit(-1);
        }
        // checking thru sockets of interest
        for(int i = 0; i <= maxfd; i++)
        {
            if(!FD_ISSET(i, &tempfds))  // not ready or in set
                continue;
            if(i == server_sock)    // new client connection attempting
            {
                int client_sock = -1;
                if((client_sock = minet_accept(server_sock, NULL))<0)
                {
                    fprintf(stderr, "ERROR ACCEPTING SOCKET!\n");
                    continue;
                }
                FD_SET(client_sock, &readfds);
                maxfd = (maxfd < client_sock) ? client_sock : maxfd;
            }
            else    // existing connection ready to be read
            {
                rc = handle_connection(i);
                if(rc < 0)
                    fprintf(stderr, "ERROR HANDLING SOCKET!\n");
                                       
                minet_close(i);
                FD_CLR(i, &readfds);
            }
        }
    }
    
    minet_close(server_sock);
	minet_deinit();
   return 0;
}
