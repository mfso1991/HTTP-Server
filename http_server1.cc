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
#define BACKLOG 10

int handle_connection(int sock);

int main(int argc, char * argv[])
{
    int server_port = -1;
    int rc          =  0;
    int sock        = -1;
    
    struct sockaddr_in saddr;
    int sock_fd;

    /* parse command line args */
    if (argc != 3)
    {
        fprintf(stderr, "usage: http_server1 k|u port\n");
        exit(-1);
    }

    server_port = atoi(argv[2]);

    if (server_port < 1500)
    {
        fprintf(stderr, "INVALID PORT NUMBER: %d; can't be < 1500\n", server_port);
        exit(-1);
    }

    /* initialize */
    if (toupper(*(argv[1])) == 'K')
    {
        minet_init(MINET_KERNEL);
    }
    else if (toupper(*(argv[1])) == 'U')
    {
        minet_init(MINET_USER);
    }
    else
    {
        fprintf(stderr, "First argument must be k or u\n");
        exit(-1);
    }

    /* initialize and make socket */
    if((sock_fd = minet_socket(SOCK_STREAM))<0)
    {
        minet_perror("server : socket");
        minet_deinit();
        exit(-1);
    }

    /* set server address*/
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(server_port);

    /* bind listening socket */
    if((minet_bind(sock_fd, (struct sockaddr_in *)&saddr))<0){
        //fail to bind
        minet_perror("server : bind");
        minet_close(sock_fd);
        minet_deinit();
        exit(-1);
    }

    /* start listening */
    if((minet_listen(sock_fd, BACKLOG))<0)
    {
        //fail to listen
        minet_perror("server : listen");
        minet_close(sock_fd);
        minet_deinit();
        exit(-1);
    }

    /* connection handling loop: wait to accept connection */
    //printf("%s\n", "before accept");

    while (1)
    {
        /* handle connections */
        //accept connection, no need to store incoming address information
        if((sock = minet_accept(sock_fd, NULL))<0)
        {
            minet_perror("server : accept");
            continue;
        }
	    rc = handle_connection(sock);
        
        printf("%d", rc);
        
        if(rc == -1)
        {
            fprintf(stderr, "%s\n", "Fail to Handle connection");
        }
        
        if(rc == -2)
        {
            break; 
        }
    }
    //close and deinitialzie sock_fd 
    minet_close(sock_fd);
    minet_deinit();
    return 0;
}

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