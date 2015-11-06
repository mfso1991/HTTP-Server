#include "minet_socket.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netdb.h>
#include <string>
#include <list>

#define FILENAMESIZE 100
#define BUFSIZE 1024
#define BACKLOG 10

typedef enum { NEW,
           READING_HEADERS,
           WRITING_RESPONSE,
           READING_FILE,
           WRITING_FILE,
           CLOSED } states;

typedef struct connection_s connection;

struct connection_s
{
    int sock;
    FILE* fd;
    char filename[FILENAMESIZE + 1];
    char buf[BUFSIZE + 1];
    //char * endheaders; //store header?
    bool ok;
    long filelen;
    states state;

    int headers_read;
    int response_written;
    int file_read;
    int file_written;
    int buf_length;
};

void read_headers(connection * con);
void write_response(connection * con);
void read_file(connection * con);
void write_file(connection * con);


fd_set readList, writeList; //enable to add/remove anytime 
std::list<connection *> connectionList;

int main(int argc, char * argv[])
{
    int server_port = -1;

    /* parse command line args */
    if (argc != 3) 
    {
	   fprintf(stderr, "usage: http_server3 k|u port\n");
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
    
    int server_sock = -1;
    if((server_sock = minet_socket(SOCK_STREAM)) < 0)
    {
        fprintf(stderr, "ERROR CREATING SOCKET.\n");
        minet_deinit();
        exit(-1);
    }
    
     /* set server address*/
    struct sockaddr_in server_addr;
    memset(&server_addr , 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET ;
    server_addr.sin_port = htons(atoi(argv[2]));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY) ;
    
    /* bind listening socket */
    if(minet_bind(server_sock, (struct sockaddr_in *)&server_addr) < 0)
    {
        fprintf(stderr, "ERROR BINDING SOCKET.\n");
        minet_close(server_sock);
        minet_deinit();
        exit(-1);
    }
    
    // listening to at most BACKLOG connections
    if(minet_listen(server_sock , BACKLOG) < 0)
    {
        fprintf(stderr, "ERROR LISTENING SOCKET!\n");
        minet_close(server_sock);
        minet_deinit();
        exit(-1);
    }

    /* connection handling loop */
    fd_set tempRead, tempWrite; //store intermidate state of sets 
    FD_ZERO(&readList);
    FD_ZERO(&writeList);
    FD_ZERO(&tempRead);
    FD_ZERO(&writeList);
    FD_SET(server_sock, &readList); //set the accept socket to the readlist
    int maxfd = server_sock;
    connectionList.clear();//clean up connection list;
    
    while (1)
    {
        /* create read and write lists */
       // printf("%s\n", "read_headers");
        tempRead = readList;
        tempWrite = writeList;
        
        /* do a select */
        if(minet_select(maxfd + 1, &tempRead, &tempWrite, NULL, NULL) < 0) // ignoring time out
        {
            fprintf(stderr, "ERROR SELECTING SOCKET!\n");
            exit(-1);
        }
        /* process sockets that are ready */
        
        //for readlist;
        for(int i = 0; i<=maxfd; i++)
        {
            if(!(FD_ISSET(i, &tempRead) || FD_ISSET(i, &tempWrite)))
                continue;
             
            //if the socket is set to read;
            if(i == server_sock)
            {
               // printf("%s\n", "accept new connection");
                int client_sock = -1;
                if((client_sock = minet_accept(server_sock, NULL))<0)
                {
                        fprintf(stderr, "ERROR ACCEPTING SOCKET!\n");
                        continue;
                }
                    //set the socket to be non-blocking
                if ((minet_set_nonblocking(client_sock))< 0)
                {
                        perror("socket non-blocking failed");
                        close(client_sock);
                        continue;
                }
                FD_SET(client_sock, &readList);//add the new connected socket to read list
                //build a new connection variable for this new connected socket
                connection *newConnection = new connection;
                memset(newConnection, 0, sizeof(connection)); //clean up the connection struct
                newConnection->sock = client_sock;
                newConnection->fd = NULL;
                newConnection->state = NEW;
                //add it to a connection sock;
                connectionList.push_back(newConnection);
                //delete newConnection;
                maxfd = (maxfd < client_sock) ? client_sock : maxfd; //update maxfd
               // printf("%s\n", "new connection");
            }
            else
            {
                //printf("%s\n", "start compare");
                //look up in the connection list, find out the read state of it
                for(std::list<connection *>::iterator it = connectionList.begin(); it != connectionList.end(); ++it)
                {
                    //if we find the matched connection with the same client socket fd
                    //check connection state
                    connection* c = *it; 
                    if(c->sock == i)
                    {
                        //printf("%s\n", "check states");
                        //if i is set to read;
                        if(c->state == CLOSED)
                        {
                              minet_close(c->sock);
                              FD_CLR(c->sock, &writeList);
                              FD_CLR(c->sock, &readList);
                              connectionList.remove(c);
                              break; 
                        }

                        if((FD_ISSET(i, &tempRead)))
                        {
                           //if we need to go to read_headers
                            if((c->state == NEW) || (c->state == READING_HEADERS))
                            {
                                read_headers(c);
                            }
                            else if(c->state == READING_FILE) //if we are reading a file 
                            {
                                read_file(c);
                            }
                        }
                        else if(FD_ISSET(i, &tempWrite))
                        {
                            if(c->state == WRITING_RESPONSE)
                                write_response(c);
                            else
                                write_file(c);
                        }
                    }
                }
            }
        }
    }
    minet_close(server_sock);
    minet_deinit();
    return 0;
}

void read_headers(connection * con)
{
    //printf("%s\n", "read_headers");
    con->state = READING_HEADERS;
    
    /* first read loop -- get request and headers*/
    int res = 0;
    std::string request;
    std::size_t found;
    do
    {
       // printf("%s\n", "reading!");

        con->headers_read += res;
        //now, has already read something into buf, then get the current length of read. start from there.
        //the length to be read should be the length of the rest of array
        std::string content(con->buf); //copy into string
        //carriage return and line feed indicating eof of headers, split headers and response
        //found records the position (length)
        found = content.find("\r\n\r\n");
        //printf("%d\n", found);
        if((con->headers_read >= 4) && found)
        {
           // printf("%s\n", "header read!");
            //printf("%d\n", found);
            request = content.substr(0, found); //request - everything before
            break;
        }

        res = minet_read (con->sock, con->buf + con->headers_read, (BUFSIZE + 1 - con->headers_read));
        
        if((errno == EAGAIN) || (res <0))
        {
            //EAGAIN, update states,  return
            con->state = READING_HEADERS;
            return;
        }
    } while(res > 0 && con->headers_read <= BUFSIZE + 1);
    
    if(errno == EAGAIN)
    {
            //EAGAIN, update states,  return
            con->state = READING_HEADERS;
            return;
    }
    
    /* parse request to get file name */
    /* Assumption: this is a GET request and filename contains no spaces*/
    found = request.find("HTTP");
    //printf("%d\n", found);
    //if input is valid and we can find the file name
    if(found > 9)
    {
        std::string file_string (request.substr(4, found - 5));
        //get file name
        strncpy(con->filename, file_string.c_str(), FILENAMESIZE + 1);
        
        /* get file name and size, set to non-blocking */
        // try opening the file
        con->fd = fopen(con->filename, "r");
        if(!con->fd)
        {
            con->ok = false;
            con->state = WRITING_RESPONSE; //update the state to writing_response
            memset(con->buf, 0, BUFSIZE+1); //clean up buf 
        }
        else
        {
            //printf("%s\n", "finishing reading request");
            con->ok = true;
            con->state = WRITING_RESPONSE; //update the state to writing_response
            memset(con->buf, 0, BUFSIZE+1); //clean up buf 
            fseek(con->fd, 0, SEEK_END); //put pointer to eof
            con->filelen = ftell(con->fd); //know the position of file pointer
            fseek(con->fd, 0, SEEK_SET);//set it back to the beginning of file
            fcntl(*(int*)con->fd, F_SETFL, O_NONBLOCK);//set to non-blocking
        }
        FD_CLR(con->sock, &readList);//start writing, remove from read list;
        FD_SET(con->sock, &writeList); //set to write socket
        write_response(con);
    }
    else
    {
            minet_perror("No valid path name");
            con->state = CLOSED;
    }
}

void write_response(connection * con) 
{
   // printf("%s\n", "write_response");
    con->state = READING_HEADERS;
    FD_CLR(con->sock, &readList);//start writing, remove from read list;
    FD_SET(con->sock, &writeList); //set to write socket

    const char * ok_response_f = "HTTP/1.0 200 OK\r\n"	\
	"Content-type: text/plain\r\n"			\
	"Content-length: %d \r\n\r\n";
    
    const char * notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"	\
	"Content-type: text/html\r\n\r\n"			\
	"<html><body bgColor=black text=white>\n"		\
	"<h2>404 FILE NOT FOUND</h2>\n"				\
	"</body></html>\n";
    
    /* send response */
    /* send headers */
   
    if(con->ok)
    {
        sprintf(con->buf, ok_response_f, con->filelen); //format ok response
    }
    else
    {
        sprintf(con->buf, notok_response);
    }
    std::string headers (con->buf);
    con-> buf_length = headers.length();

    int res = 0; 
    while(con->buf_length)
    {
        res = minet_write(con->sock, con->buf + (con->response_written % (BUFSIZE +1)), con->buf_length);
        if(res < 0)
        {
            minet_perror("Fail to write to the clients : response");
            con->state = CLOSED;
            break;
        }
        if(errno == EAGAIN)
        {
            //EAGAIN, update states,  return
            con->state = WRITING_RESPONSE;
            return;
        }
        con->buf_length -= res;
        con->response_written +=res;
    }
    
    if(errno == EAGAIN)
    {
            //EAGAIN, update states,  return
            con->state = WRITING_RESPONSE;
            return;
    }

    if(con->response_written == con-> headers_read)
    {
        memset(con->buf, 0, BUFSIZE+1); //clear up buf
        FD_CLR(con->sock, &writeList);
        FD_SET(con->sock, &readList);
        con->buf_length = 0;
        read_file(con);
    }
}

void read_file(connection * con)
{
    //printf("%s\n", "read_file");
    con->state = READING_FILE;
    int res = 0;
    while(con->buf_length <= BUFSIZE+1)
    {
        res = fread(con->buf + con->buf_length, sizeof(char), BUFSIZE + 1 - con->buf_length, con->fd);
        //no more to read
        if(res == 0)
        {
            //printf("%s\n", con->buf);
            break;
        }
        else if(res<0)
        {
            minet_perror("Fail to read from the file");
            con->state = CLOSED;
            return;
        }
        if(errno == EAGAIN)
        {
            //EAGAIN, update states,  return
            con->state = READING_FILE;
            return;
        }
        con->file_read += res;
        con->buf_length +=res;
    }
    if(errno == EAGAIN)
    {
            //EAGAIN, update states,  return
            con->state = READING_FILE;
            return;
    }
    //if length of file  == what has read, finish reading already, close file descriptor 
    if(con->file_read == con->filelen)
    {
        fclose(con->fd);
         //return; //close file
    }
    //send what we read to the connected socek;
    // printf("%d\n", con->buf_length);
    // printf("%s\n", con->buf);
    con->state = WRITING_FILE;
    FD_CLR(con->sock, &readList);//start writing, remove from read list;
    FD_SET(con->sock, &writeList); //set to write socket
    write_file(con);
}

void write_file(connection * con) 
{
    //printf("%s\n", "write_file");
    con->state = WRITING_FILE;
    int res = 0;
    while(con->buf_length)
    {
        res = minet_write(con->sock, con->buf + (con->file_written % (BUFSIZE +1)), con->buf_length);
        if(res <= 0)
        {
            minet_perror("Fail to write to the clients");
            break;
        }
        if(errno == EAGAIN)
        {
            //EAGAIN, update states,  return
            con->state = WRITING_FILE;
            return;
        }
        con->buf_length -= res;
        con->file_written += res;
    }
    if(errno == EAGAIN)
    {
        //EAGAIN, update states,  return
        con->state= WRITING_FILE;
        return;
    }
    if(con->file_written == con->filelen)
    {
        //remove conection, remove from set, close socket
        con->state = CLOSED;
        return;
    }
    //go back to read if all the characters in buffer are consumed
    if(con->buf_length == 0)
    {
        memset(con->buf, 0, BUFSIZE+1); //clear up buf
        FD_CLR(con->sock, &writeList);
        FD_SET(con->sock, &readList);
        read_file(con);
    }
}
