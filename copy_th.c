/* port driver for file copy  */

#define STDIN 0
#define STDOUT 1
#define BLK_SIZE 0x700000
#include <endian.h>
#include <linux/joystick.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>     /* pthread functions and data structures */
#include "binn.h"
#include <sys/types.h>
#include <sys/stat.h>


// #include "erl_comm.h"

u_int64_t bytes_copied=0;
char file_copy_finished=0;  // false
static pthread_mutex_t mtx_read = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_write = PTHREAD_MUTEX_INITIALIZER;

void do_error(int ReturnCode, char *msg)
{
  char buf[1000];
  fprintf(stderr,"%s",msg);
  // sprintf(buf,"error:%s",msg);
  // write_port(buf,1000);
  exit(ReturnCode);
}

int send_to_erlang(char code, u_int64_t data)
{
  binn *list;
  char packet_len;
  // fprintf(stderr,"Entering send_to_erlang\n");
  list=binn_list();
  binn_list_add_uint8(list,code);
  binn_list_add_uint64(list,data);
  /* send to erlang prefixed by the amount of data to send
     note that multibyte lengths need to be converted to 
     network order, but as we are using a single byte it
     is currently unnecessary */
  packet_len=binn_size(list);
  write(1,&packet_len,1);
  write(1,binn_ptr(list),packet_len);
}
void send_copy_update(u_int64_t bytes_copied)
{
  send_to_erlang(1,bytes_copied);
};

void send_copy_complete(u_int64_t bytes_copied)
{
  send_to_erlang(2,bytes_copied);
}
void copy_file_blocks (int read_fd, int write_fd)
{
  fprintf(stderr,"copy file blocks started\n");
  fprintf(stderr,"input fd is %d ",read_fd);
  char data[BLK_SIZE];
  ssize_t bytes_read;
  // take care to avoid race conditions around
  // file_copy_finished flag and the while loop.  Protected by mtx_read.
  fprintf(stderr,"obtaining mutex mtx_read\n"),
  pthread_mutex_lock(&mtx_read);
  fprintf(stderr,"starting while loop");
  while
    (
     (! file_copy_finished)
     &&
     (( bytes_read = read(read_fd,data,BLK_SIZE)) > 0)
     )
    {
      // fprintf(stderr,"read %li characters\n",bytes_read);
      // locking write before releasing read ensures
      // blocks are output in the order they were read
      pthread_mutex_lock(&mtx_write);
      pthread_mutex_unlock(&mtx_read);
      // fprintf(stderr,"writing blocks\n");
      write(write_fd,data,bytes_read);
      // bytes_copied protected by mtx_write
      bytes_copied += bytes_read;
      pthread_mutex_unlock(&mtx_write);

      send_copy_update(bytes_copied);
     
      pthread_mutex_lock(&mtx_read);
    }
  file_copy_finished = 1;
  pthread_mutex_unlock(&mtx_read);
  if(bytes_read==-1) 
    fprintf(stderr,"Oh dear, something went wrong with read()! %s\n", strerror(errno));

}

void *th_copy_file_blocks(void *Params)
{
  // this function is just a threading wrapper around copy_file_blocks
  int *fds;
  fds = (int *) Params;
  
  fprintf(stderr,"calling second thread copy_file_blocks\n");

  copy_file_blocks(fds[0],fds[1]);
}

int main(int argc, char **argv)
{
  /* get the device id from parameter list */
  char *in_filename;
  char *out_filename;
  int in_fd;
  int out_fd;
  char error_msg[1000];

  if (argc > 1)
    {
      in_filename = argv[1];
      out_filename = argv[2];
    }

    // do_error(EXIT_NO_JOYSTICK_GIVEN,"No device parameter\n");
  /* open fd to special device in blocking mode */
  
  fprintf(stderr,"input file is %s \n",in_filename);
  in_fd = open(in_filename, O_RDONLY);
  fprintf(stderr,"input fd is %d \n",in_fd);
  if (in_fd < 0)
    {
      sprintf(error_msg,"fail opening device %s\n%s\n",in_filename,strerror(errno)); 
      // do_error(EXIT_OPENFAILED,error_msg);
    };
  
  fprintf(stderr,"output file is %s \n",out_filename);
  out_fd = open(out_filename, O_WRONLY | O_CREAT, 0644);
  fprintf(stderr,"output fd is %d \n",out_fd);  
  if (out_fd < 0)
    {
      sprintf(error_msg,"fail opening device %s\n%s\n",out_filename,strerror(errno)); 
      // do_error(EXIT_OPENFAILED,error_msg);
    };
      // ok
  
  pthread_t copy1_id;
  pthread_t copy2_id;
  int fds [2] = {in_fd,out_fd};
  if (2 == ((in_fd > 0) + (out_fd > 0)))
	    {
	      fprintf(stderr,"Calling copy_file_blocks\n");
	      pthread_create(&copy1_id, NULL, th_copy_file_blocks, (void*) fds);
	      copy_file_blocks(in_fd,out_fd);
	      //pthread_create(&copy2_id, NULL, copy_file_blocks, (void*)&fds);
	      /* code here to start thread to interact with Erlang */
	      pthread_join(copy1_id,NULL);
	    }
  send_copy_complete(bytes_copied);
  return 0;
}


