#include "chatServer.h"
#include <stdlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <netinet/in.h>

#define diff 48
int castStringToInt(char* s);
static int end_server = 0;

void intHandler(int SIG_INT) {
    /* use a flag to end_server to break the main loop */
    end_server =1;
}

int main (int argc, char *argv[])
{

    signal(SIGINT, intHandler);

    conn_pool_t* pool = malloc(sizeof(conn_pool_t));
    init_pool(pool);
    int port = castStringToInt(argv[1]);
    if(port <= 0)
        printf("Usage: server <port>");
    if(port < 1 || port > 65536)
        printf("Usage: server <port>");



    /*************************************************************/
    /* Create an AF_INET stream socket to receive incoming      */
    /* connections on                                            */
    /*************************************************************/

    int fd;
    if((fd = socket(PF_INET,SOCK_STREAM,0)) < 0){
        free(pool);
        perror("socket");
        exit(1);
    }


    /*************************************************************/
    /* Set socket to be nonblocking. All of the sockets for      */
    /* the incoming connections will also be nonblocking since   */
    /* they will inherit that state from the listening socket.   */
    /*************************************************************/


    int on = 1;
    ioctl(fd, (int)FIONBIO, (char *)&on);



    /*************************************************************/
    /* Bind the socket                                           */
    /*************************************************************/


    struct sockaddr_in srv;
    srv.sin_family = PF_INET;
    srv.sin_port = htons(port);
    srv.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(fd,(struct sockaddr*)&srv, sizeof(srv))< 0){
        free(pool);
        perror("bind");
        exit(1);
    }

    /*************************************************************/
    /* Set the listen back log                                   */
    /*************************************************************/


    if(listen(fd,5) < 0){
        free(pool);
        perror("listen");
        exit(1);
    }

    /*************************************************************/
    /* Initialize fd_sets  			                             */
    /*************************************************************/

    FD_SET(fd,&pool->read_set);
    pool->maxfd = fd;

    /*************************************************************/
    /* Loop waiting for incoming connects, for incoming data or  */
    /* to write data, on any of the connected sockets.           */
    /*************************************************************/
    do
    {
        /**********************************************************/
        /* Copy the master fd_set over to the working fd_set.     */
        /**********************************************************/
        memcpy(&pool->ready_read_set,&pool->read_set, sizeof(pool->read_set));
        memcpy(&pool->ready_write_set,&pool->write_set, sizeof(pool->write_set));

        /**********************************************************/
        /* Call select() 										  */
        /**********************************************************/

        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
       pool->nready = select(pool->maxfd+1,&pool->ready_read_set,&pool->ready_write_set,NULL,NULL);
       if(pool->nready < 0){
           break;
       }
        //printf("after select\n");


        /**********************************************************/
        /* One or more descriptors are readable or writable.      */
        /* Need to determine which ones they are.                 */
        /**********************************************************/
        int count = 0;

        for (int i = 3 ; i < pool->maxfd+1 && count < pool->nready ; i++)
        {
            /* Each time a ready descriptor is found, one less has  */
            /* to be looked for.  This is being done so that we     */
            /* can stop looking at the working set once we have     */
            /* found all of the descriptors that were ready         */

            /*******************************************************/
            /* Check to see if this descriptor is ready for read   */
            /*******************************************************/
            if (FD_ISSET(i,&pool->ready_read_set))
            {
                count++;
                /***************************************************/
                /* A descriptor was found that was readable		   */
                /* if this is the listening socket, accept one      */
                /* incoming connection that is queued up on the     */
                /*  listening socket before we loop back and call   */
                /* select again. 						            */
                /****************************************************/

                if(i == fd){ // in case I have new connection
                    int newfd = accept(fd,NULL, NULL);
                    int on1 = 1;
                    ioctl(fd, (int)FIONBIO, (char *)&on1);
                    add_conn(newfd,pool);
                    printf("New incoming connection on sd %d\n", newfd);
                    FD_SET(newfd,&pool->read_set);
                }
                else{ // in case one of the client write to me
                    /****************************************************/
                    /* If this is not the listening socket, an 			*/
                    /* existing connection must be readable				*/
                    /* Receive incoming data his socket             */
                    /****************************************************/
                    char buff[BUFFER_SIZE];
                    char buff1[BUFFER_SIZE];
                    memset(buff,'\0',BUFFER_SIZE);
                    memset(buff1,'\0',BUFFER_SIZE);
                    ssize_t read1;
                    printf("Descriptor %d is readable\n", i);
                    read1 = read(i,buff, BUFFER_SIZE);

                    if(read1 > 0){ // there is data to read.
                        for(int j = 0 ; j < BUFFER_SIZE ; j++){
                            if(buff[j] == '\r' && buff[j+1] == '\n'){
                                buff1[j] ='\r';
                                buff1[j+1] = '\n';
                                buff1[j+2] = '\0';
                                break;
                            }

                            buff1[j] = buff[j];
                        }
                        printf("%d bytes received from sd %d\n", (int)strlen(buff1), i);

                        /**********************************************/
                        /* Data was received, add msg to all other    */
                        /* connectios					  			  */
                        /**********************************************/
                        add_msg(i,buff1, (int)strlen(buff1),pool);
                    }
                    else if(read1 == 0){ // read return 0 , client close connection.
                        /* If the connection has been closed by client 		*/
                        /* remove the connection (remove_conn(...))    		*/
                        printf("Connection closed for sd %d\n",i);
                        remove_conn(i,pool);

                    }
                }

            } /* End of if (FD_ISSET()) */

            /*******************************************************/
            /* Check to see if this descriptor is ready for write  */
            /*******************************************************/
            if (FD_ISSET(i,&pool->ready_write_set)) {
                count++;
                /* try to write all msgs in queue to sd */
                write_to_client(i,pool);
            }
            /*******************************************************/


        } /* End of loop through selectable descriptors */

    } while (end_server == 0);

    /*************************************************************/
    /* If we are here, Control-C was typed,						 */
    /* clean up all open connections					         */
    /*************************************************************/
    for(int j = fd+1; j<= pool->maxfd ; j++){
        remove_conn(j,pool);
    }
    close(fd);
    free(pool);

    return 0;
}

//initialized all fields
int init_pool(conn_pool_t* pool) {
    pool->maxfd = 0;
    pool->conn_head = NULL;
    pool->nr_conns = 0;
    pool->nready = 0;
    FD_ZERO(&pool->write_set);
    FD_ZERO(&pool->read_set);
    FD_ZERO(&pool->ready_write_set);
    FD_ZERO(&pool->ready_read_set);
    return 0;
}
/*
* 1. allocate connection and init fields
* 2. add connection to pool
*/
int add_conn(int sd, conn_pool_t* pool) {
    conn_t * con =(conn_t*) malloc(sizeof(conn_t));
    if(con == NULL)
        return -1;
    con->fd = sd;
    con->next = NULL;
    con->prev = NULL;
    con->write_msg_head = NULL;
    con->write_msg_tail = NULL;
    if(sd > pool->maxfd)//in case I need to update max fd
        pool->maxfd = sd;

    pool->nr_conns+=1;//increment the number of the active connections by 1

    conn_t * temp = pool->conn_head;

    if(temp == NULL){ // in case the pool is empty
        pool->conn_head = con;
        return 0;
    }

    while (temp->next !=NULL)//add connection to the end of the pool
        temp = temp->next;

    temp->next = con;
    con->prev = temp;

    return 0;
}

/*
1. remove connection from pool
2. deallocate connection
3. remove from sets
4. update max_fd if needed
*/
/// need to free all the msg.
/// need to remove from sets
int remove_conn(int sd, conn_pool_t* pool) {
    if(pool->conn_head == NULL)
        return 0;
    conn_t * temp = pool->conn_head;
    while (temp != NULL){
        if(temp->fd == sd){ // in case I found the connection to be removed
            printf("removing connection with sd %d \n", sd);

            msg_t * tmpMsg = temp->write_msg_head;
            while (tmpMsg != NULL){
                msg_t * tmp1 = tmpMsg->next;
                free(tmpMsg->message); /// need to check if the buffers are static or dinamic.
                free(tmpMsg);
                tmpMsg = tmp1;
                temp->write_msg_head = tmpMsg;
                if(temp->write_msg_head == NULL)
                    temp->write_msg_tail = NULL;
            }

            if(pool->nr_conns ==1){ // in case I have 1 active connection
                free(pool->conn_head);
                pool->maxfd = 3;
                pool->nr_conns = 0;
                pool->nready = 0;
                pool->conn_head = NULL;
                FD_CLR(sd,&(pool->read_set));
                FD_CLR(sd,&(pool->write_set));
                close(sd);
                return 0;
            }
            else{ // in case I need to remove not the head of the connection
                if(temp->prev != NULL)
                    temp->prev->next = temp->next;
                if(temp->next != NULL)
                    temp->next->prev = temp->prev;

                if(temp == pool->conn_head)
                    pool->conn_head = temp->next;

                free(temp);
                temp = NULL;
                pool->nr_conns-=1;

                if(pool->maxfd == sd){
                    temp = pool->conn_head;
                    int max = 0;
                    while (temp!=NULL){
                        if(temp->fd > max)
                            max = temp->fd;
                        temp = temp->next;
                    }
                    pool->maxfd = max;
                }

                FD_CLR(sd,&(pool->read_set));
                FD_CLR(sd,&(pool->write_set));
                close(sd);
                return 0;
            }
        }
        else{
            temp = temp->next;
        }
    }
    return -1;


    //return 0;
}
/*
 1. add msg_t to write queue of all other connections
 2. set each fd to check if ready to write
*/
int add_msg(int sd,char* buffer,int len,conn_pool_t* pool) {
    if(pool->conn_head == NULL)
        return -1;





    conn_t * temp = pool->conn_head;

    while (temp != NULL){
        if(temp->fd != sd){
            msg_t * msgToAdd = (msg_t*) malloc(sizeof(msg_t));
            if(msgToAdd == NULL)
                return -1;
            msgToAdd->message = (char *)malloc(sizeof(char)*(len+1));
            //msgToAdd->message = buffer;
            strcpy(msgToAdd->message,buffer);
            msgToAdd->size = len;
            msgToAdd->prev = NULL;
            msgToAdd->next = NULL;

            if(temp->write_msg_head == NULL){
                temp->write_msg_head = msgToAdd;
                temp->write_msg_tail = msgToAdd;
            }
            else{
                temp->write_msg_tail->next = msgToAdd;
                msgToAdd->prev = temp->write_msg_tail;
                temp->write_msg_tail = msgToAdd;
            }
            FD_SET(temp->fd,&pool->write_set);

        }
        temp = temp->next;
    }



    return 0;
}
/*
1. write all msgs in queue
2. deallocate each writen msg
3. if all msgs were writen successfully, there is nothing else to write to this fd
*/
int write_to_client(int sd,conn_pool_t* pool) {
    if(pool->conn_head == NULL)
        return -1;
    conn_t * temp = pool->conn_head;

    while (temp!= NULL && temp->fd != sd){ // I make temp point to the connection I need to write to
        temp = temp->next;
    }
    if(temp == NULL)// in case the sd different from all the fd in the connections
        return -1;
    msg_t * tmpMsg = temp->write_msg_head;
    while (tmpMsg != NULL){
        write(sd,tmpMsg->message,tmpMsg->size);
        msg_t * tmp1 = tmpMsg->next;
        free(tmpMsg->message);
        free(tmpMsg);
        tmpMsg = NULL;
        tmpMsg = tmp1;
        temp->write_msg_head = tmpMsg;
        if(temp->write_msg_head == NULL)
            temp->write_msg_tail = NULL;
    }
    FD_CLR(sd,&pool->write_set);

    return 0;
}
int castStringToInt(char* s){
    int num = 0;
    for(int i = 0 ; i < strlen(s); i++){
        if(s[i] < '0' || s[i] > '9')
            return -1;
        if(i == 0)
            num+=s[i]-diff;
        else{
            num*=10;
            num+=s[i]-diff;
        }
    }
    return num;
}

