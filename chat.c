#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
//#include <openssl/bn.h>
//#include <openssl/dh.h>
#include "dhread.h"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#define BACKLOG_SIZE 1
#define SAI struct sockaddr_in
#define SA struct sockaddr
#define DIM_BUF 512
#define DIM_IP 16
#define DIM_MAIL 255
#define DIM_ENV 646
#define CERT_NAME "05.pem"
#define PRIV_KEY "a_privkey.pem"
#define FIELD_SEPARATOR 32
#define HELO_MSG1 1
#define HELO_MSG2 2
#define CHAT_MSG 3
#define TMP_CERT "/tmp/tmp_cert.pem"
#define AUTO_CERT "cacert.pem"
int create_socket(SAI* sa,int port,char *ip){
	if ((port <= 0) || (port > 65535)) {
		errno = EINVAL;
		return -2; // bad port or ip
	}
			/* New socket creation */
	int sk, ret;		
	sk = socket(AF_INET, SOCK_STREAM, 0);
	if(sk == -1){
		errno = EINVAL;
		return -1;
	}
	/* The socket is binded with the IP address and the port number */
	memset(sa, 0, sizeof(SAI)); 
	sa->sin_family = AF_INET;
	if(ip == NULL){
		sa->sin_addr.s_addr = htonl(INADDR_ANY);
	}else{
		if ((ret = inet_pton(AF_INET, ip, &(sa->sin_addr))) == 0) {
			errno = EINVAL;
			close(sk);
			return -2; //-2 bad ip or port
		}
		if (ret == -1) {
			close(sk);
			return -1;
		}
	} 
	sa->sin_port = htons(port);
	return sk;
}
char* read_common_name(FILE* fp){
	X509* cert = PEM_read_X509(fp, NULL, NULL, NULL);
	X509_NAME* name;
	int ret;
	char* identifier;
	char* my_mail = (char*)calloc(1,DIM_MAIL);
	name = X509_get_subject_name(cert);
	identifier = X509_NAME_oneline(name, NULL, 0);
	if((ret=get_common_name(my_mail,identifier)) < 0){
		return NULL;
	}
	return my_mail;
}
unsigned char* prepare_cert(FILE* afp,unsigned int* cert_len){
	struct stat file_info;
	int fd;
	int ret;
	FILE *fp;
	unsigned char* cert;
	fp = fopen(CERT_NAME,"r");
	if(fp == NULL){
		return NULL;
	}
	fd = fileno(fp);
	if(fstat(fd,&file_info) < 0){
		return NULL;
	}
	cert = (unsigned char*)calloc(1,file_info.st_size);
	if(fread(cert,1,file_info.st_size,fp)<file_info.st_size){
		return NULL;
	}
	//ret = fread(cert,1,file_info.st_size,fp);
	//perror("error on fread in prepare_cert\n");
	*cert_len = file_info.st_size;
	fclose(fp);
	return cert;
}
/*It returns -1 on erro, 0 on mismatching on certificate, 1 on success*/
int verify_name(FILE* fp,unsigned char *hello,unsigned int hello_len,unsigned char *sign,unsigned int sign_len,unsigned char *cert, unsigned int cert_len,unsigned char** pub_buf,unsigned int *pubbuf_len,X509_STORE* str,int* nonce){	
	int sheet_len,ret;
	uint32_t tmp;
	char read_mail[DIM_MAIL],temp_mail[DIM_MAIL],*cert_mail = NULL;
	X509_STORE_CTX* cert_ctx = NULL;
	EVP_PKEY* evp = EVP_PKEY_new();
	EVP_MD_CTX* ctx = NULL;
	X509* cert = PEM_read_X509(fp,NULL,NULL,NULL);
	*pub_buf = NULL;
	if((cert_ctx=X509_STORE_CTX_new())==NULL){
		goto fail;
	}
	if(X509_STORE_CTX_init(cert_ctx,str,cert,NULL)<=0){
		goto fail;
	}
	if(X509_verify_cert(cert_ctx)==0){
		goto fail;	
	}
	X509_STORE_CTX_cleanup(cert_ctx);
	X509_STORE_CTX_free(cert_ctx);
	ctx  = (EVP_MD_CTX*)calloc(1,sizeof(EVP_MD_CTX));
	EVP_MD_CTX_init(ctx);
	evp = X509_get_pubkey(cert);
	if(EVP_VerifyInit(ctx,EVP_sha512())==0){
		ret = -1;
		goto fail;
	}
	if(EVP_VerifyUpdate(ctx,cipher_buf,cipher_len)==0){
		ret = -1;
		goto fail;
	}
	if((ret=EVP_VerifyFinal(ctx,sign_buf,sign_len,evp))<=0){
		goto fail;
	}
	cert_mail = read_common_name(fp);//set it free later
	sscanf(hello,"%s%s",temp_mail,read_mail);
	sheet_len = strlen(temp_mail)+strlen(read_mail)+2;
	*pubbuf_len = hello_len - sheet_len;
	tmp = *((uint32_t *)(hello+sheet_len));
	*nonce = ntohl(tmp);
	sheet_len+=sizeof(tmp);
	*pub_buf = (unsigned char*)calloc(1,*pubbuf_len);
	memcpy(*pub_buf,hello+sheet_len,*pubbuf_len);
	if(strlen(cert_mail)!=strlen(read_mail)){
		ret = 0;
		goto fail;
	}
	if(strncmp(cert_mail,read_mail,strlen(cert_mail))!=0){
		ret = 0;
		goto fail;
	}
	free(ctx);
	fclose(fp);
	EVP_PKEY_free(evp);
	free(cert_mail);
	return 1;
	fail:
		fclose(fp);
		if(cert_mail!=NULL){
			free(cert_mail);
		}
		if(cert_ctx!=NULL){
			X509_STORE_CTX_cleanup(cert_ctx);
			X509_STORE_CTX_free(cert_ctx);
		}
		if(ctx!=NULL){
			free(ctx);
		}	
		if(*pub_buf!=NULL){
			free(*pub_buf);
		}
		EVP_PKEY_free(evp);
		return ret;
}	
int send_msg(int sk, unsigned char* buf, int num_bytes,char format) {
	uint32_t tmp;
    unsigned char* msg;
	int ret, msg_len;
    if (sk < 0 || buf == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    msg = (unsigned char*)calloc(1, sizeof(tmp) + num_bytes+1);
	tmp = htonl((uint32_t)num_bytes + 1);
	memcpy(msg, &tmp, sizeof(tmp));
	msg[4] = format;
    memcpy(msg + sizeof(tmp)+1, buf, num_bytes);
    msg_len = num_bytes + sizeof(tmp) + 1;
	if ((ret = send(sk,msg,msg_len,0)) < msg_len) {
        free(msg);
        return -1;
    }
    free(msg);
	return ret;
}
/*
 * This function receive a message from a socket
 * It returns the length of the payload in bytes or -1 if an error occurred.
 * This function set errno in case of error.
 */
int recv_msg(int sk, unsigned char** buf,char* format) {
    int ret, buflen;
    uint32_t tmp;
    
    if (sk < 0) {
        errno = EINVAL;
        return -1;
    }
    
    if ((ret = recv(sk, &tmp, sizeof(tmp), MSG_WAITALL)) < sizeof(tmp)) {
        return -1;
    }
    buflen = ntohl(tmp);
    *buf = (unsigned char*)calloc(1, buflen);
    
    if ((ret = recv(sk, *buf, buflen, MSG_WAITALL)) < buflen) {
        free(*buf);
        return -1;
    }
    *format = (*buf)[0];
    memmove(*buf,(*buf)+1,buflen-1);//we remove the format from buf
    return ret-1;//It returns the effective buflen
}

/* 
 * Create the hello + certificate message and sends it.
 * It returns the number of bytes sent or -1 if an error occured. This function
 * set the value of errno in case of error.
 */
int send_hello(int sk, unsigned char* hello, unsigned int hello_len, \
                    unsigned char* sign, unsigned int sign_len, \
                    unsigned char* cert, unsigned int cert_len)
{
    uint32_t tmp;
    int num_bytes, ret;
    unsigned char* msg;
    
    if (sk < 0 || hello == NULL || sign == NULL || cert == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    /* this message contains the hello string length, the hello string, the
     * hello string signature length, the hello string signature, the 
     * certificate length and the certificate
     */
    msg = (unsigned char*)calloc(1, hello_len + sign_len + cert_len + \
			3*sizeof(tmp));
   
    /* appending hello */
    tmp = htonl(hello_len);
    memcpy(msg, &tmp, sizeof(tmp));
    num_bytes = sizeof(tmp);
    memcpy(msg + num_bytes, hello, hello_len);
    num_bytes += hello_len;
    
    /*appending signature*/
    tmp = htonl(sign_len);
    memcpy(msg + num_bytes, &tmp, sizeof(tmp));
    num_bytes += sizeof(tmp);
    memcpy(msg + num_bytes, sign, sign_len);
    num_bytes += sign_len;
    
    /*appending certificate*/
    tmp = htonl(cert_len);
    memcpy(msg + num_bytes, &tmp, sizeof(tmp));
    num_bytes += sizeof(tmp);
    memcpy(msg + num_bytes, cert, cert_len);
    num_bytes += cert_len;
    
    /* sending */
    if ((ret = send_msg(sk, msg, num_bytes,(char)HELO_MSG1)) <= 0) {
        free(msg);
        return -1;
    }
    free(msg);
    return ret;
}

/*
 * Receive an hello + certificate message
 * If something goes wrong, it sets errno and returns -1, otherwise it returns 
 * the number of read byte. The given pointers are allocated using calloc and 
 * must be freed.
 */
int recv_hello(int sk, unsigned char** hello, unsigned int* hello_len, \
					unsigned char** sign, unsigned int *sign_len, \
                    unsigned char** cert, unsigned int* cert_len)
{
    uint32_t tmp;
    int msg_len, ret, pos;
    unsigned char* msg;
    char format;
    
    if (sk < 0) {
        errno = EINVAL;
        return -1;
    }
    
    if ((msg_len = recv_msg(sk, &msg,&format)) <= 0) {
        return -1;
    }
    if(format != HELO_MSG1){
    	errno = EINVAL;
    	return -1;
    }
    /* reading hello */
    memcpy(&tmp, msg, sizeof(tmp));
    *hello_len = (unsigned int)ntohl(tmp);
    pos = sizeof(tmp);
    
    *hello = (unsigned char*)calloc(1, *hello_len);
    memcpy(*hello, msg + pos, *hello_len);
    pos += *hello_len;
    
    /* reading hello signature */
    memcpy(&tmp, msg + pos, sizeof(tmp));
    *sign_len = (unsigned int)ntohl(tmp);
    pos += sizeof(tmp);
    
    *sign = (unsigned char*)calloc(1, *sign_len);
    memcpy(*sign, msg + pos, *sign_len);
    pos += *sign_len;
    
    /* reading certificate */
    memcpy(&tmp, msg + pos, sizeof(tmp));
    pos += sizeof(tmp);
    *cert_len = (int)ntohl(tmp);
    
    *cert = (unsigned char*)calloc(1, *cert_len);
    memcpy(*cert, msg + pos, *cert_len);
    pos += *cert_len;
    
    return pos;
}

/* sign the buffer passed as argument, returns the length of the signature
/* else -1 on error*/
int sign_hello(char* hello_buf,unsigned int hello_len,unsigned char** sign_buf){
	EVP_MD_CTX* ctx = NULL;
	unsigned int sign_len;
	EVP_PKEY* evp = EVP_PKEY_new();
	FILE* fp;
    *sign_buf = NULL;
    ctx = (EVP_MD_CTX*)calloc(1,sizeof(EVP_MD_CTX));
	EVP_MD_CTX_init(ctx);
	OpenSSL_add_all_algorithms();
	if((fp=fopen(PRIV_KEY,"r"))==NULL){
		goto fail;
	}
	if((evp=PEM_read_PrivateKey(fp,NULL,NULL,NULL))==NULL){
		goto fail;
	}
    *sign_buf = (unsigned char*)calloc(1,EVP_PKEY_size(evp));
   	if(EVP_SignInit(ctx,EVP_sha512())==0){
        goto fail;
	}
	if(EVP_SignUpdate(ctx,hello_buf,hello_len)==0){
		goto fail;
	}
	if(EVP_SignFinal(ctx,*sign_buf,&sign_len,evp)==0){
		goto fail;
	}
	
	EVP_MD_CTX_cleanup(ctx);
	free(ctx);
	EVP_PKEY_free(evp);
	return sign_len;
    
fail:
	EVP_MD_CTX_cleanup(ctx); 
	free(ctx);
    if (*sign_buf != NULL) {
        free(*sign_buf);
    }
    return -1;
}
int prepare_and_sign_hello(char* mail1,unsigned int length1,char *mail2,unsigned int length2,int nonce,unsigned char* pubkey, unsigned int publen,unsigned char** hello_buf, unsigned int* hello_len,unsigned char** sign_buf,unsigned int* sign_len){
	/*create the hello*/
	uint32_t tmp;
	*hello_len = length1+length2+2+sizeof(tmp)+publen;
	*hello_buf = (unsigned char*)calloc(1,*hello_len);
	unsigned int pos;
	memcpy(*hello_buf,mail1,length1);
	*(*hello_buf+length1) = (unsigned char)FIELD_SEPARATOR;
	pos = length1 + 1;
	memcpy(*hello_buf + pos, mail2, length2);
	pos += length2;
	*(*hello_buf + pos) = (unsigned char)FIELD_SEPARATOR;
	pos++;
	tmp = htonl(nonce);
	memcpy(*hello_buf + pos, &tmp, sizeof(tmp));
	pos += sizeof(tmp);
	memcpy(*hello_buf + pos, pubkey, publen);
	pos+=publen;
	/*sign the hello*/
	if((*sign_len=sign_hello(*hello_buf,*hello_len,sign_buf,sign_len)<=0)){
		return -1;
	}
	return 1;
}
DH* dh_genkey(){
	DH *dh = get_dh1024();
	if(DH_generate_key(dh)!=1){
		DH_free(dh);
		return NULL;
	}
	return dh;
}
/*
 * get the common name field value from an identifier string.
 * The output string is null terminated.
 * It returns -1 and set errno if an error occured. Otherwise, if everything is
 * ok, it returns the number of byte written into dest.
 */
int get_common_name(char* dest, const char* src)
{
	char* field_start = "/CN=";
	int i, j, byte_count = 0;

	if (!src || !dest) {
		goto fail;
	}
	
	i = 0;
	while (src[i] != '\0') {
		/* starting comparison */
		j = 0;
		while (field_start[j] == src[i + j] && field_start[j] != '\0') {
			/* continue until the end of the field name is over */
			j++;
		}
		/* if field_start[j] == '\0' we have found the common name */
		if (field_start[j] == '\0') {
			i = i + j;
			j = 0;
			/* copying common name until a new field is found */
			while (src[i] != '/') {
				dest[j] = src[i];
				i++;
				j++;
				byte_count++;
			}
			/* if the copy is over */
			if (src[i] == '/') {
				dest[byte_count] = '\0';
				return byte_count;
			}
		}
		
		i++;
	}
	/* this point is reached only if we were unable to find the 
	 * common name
	 */
fail:	errno = EINVAL;
	return -1;
}

/* This function starts a connection with the given peer.
 * It performs various checks and returns -1 if the connection can not be established
 * but the program must not terminate, -2 if the program fails completely.
 * It returns 1 on success.
 */
int init_connection (char* buf,int *peer_sk,int mynonce,X509_STORE* str){
	char ipbuf[DIM_IP], peer_mail[DIM_MAIL],*my_mail,*received_mail;
	DH* dh;
	EVP_PKEY* evp;
	unsigned int mypub_len,peerpub_len,hello_len,sign_len,cert_len;
	unsigned char *mypub_buf,*peerpub_buf,*hello_buf,*sign_buf,*cert_buf;
	int peernonce,peer_port,ret;
	FILE* fp;
	SAI peer_addr;
	if((fp = fopen(CERT_NAME, "r"))==NULL){
		return -2;
	}
	
	sscanf(buf,"%s%d%s",ipbuf,&peer_port,peer_mail);
	if((*peer_sk=create_socket(&peer_addr,peer_port,ipbuf)) < 0){
		return *peer_sk;
	}	
 	if((ret = connect(*peer_sk,(SA*)&peer_addr,sizeof(peer_addr)) < 0)){
 		return ret;
 	}
 	if((my_mail=read_common_name(fp))==NULL){
		return -1;
	}
	if((dh=dh_genkey())==NULL){
		errno = EINVAL;
		return -1;	
	}
	mypub_buf = (unsigned char*)calloc(1,BN_num_bytes(dh->pub_key));
	mypub_len = BN_bn2bin(dh->pub_key,mypub_buf);
	//input:my_mail,length1,peer_mail,length2,mynonce,mypub_buf,mypub_len
	//output:hello_buf,hello_len,sign_buf,sign_len
	if(prepare_and_sign_hello(my_mail,strlen(my_mail),peer_mail,\ 
		strlen(peer_mail),mynonce,mypub_buf,mypub_len,&hello_buf,\
		&hello_len,&sign_buf,&sign_len)<0){
		return -1;
	}
 	cert_buf = prepare_cert(fp,&cert_len);
 	if(cert_buf == NULL){
 		return -1;
 	}
 	if(fclose(fp)!=0){
 		return -1;
 	}
  	if(send_hello(*peer_sk,hello_buf, hello_len, sign_buf, sign_len, cert_buf, cert_len)<=0){
 		return -1;
 	}
	free(hello_buf);
	free (sign_buf);
	free(cert_buf);
	/*
	 * the next call set the correct hello_buf, hello_len, sign_buf,sign_len, 
	 * cert and cert_len
	 */
	if((ret=recv_hello(*peer_sk,&hello_buf, &hello_len, &sign_buf, &sign_len, &cert_buf, &cert_len))<0){
		return -1;
	}	
	if((fp=fopen(TMP_CERT,"w+"))==NULL){
		return -1;
	}
	if(fwrite(cert_buf,cert_len,1,fp)<cert_len){
		return -1;
	}
	if ((ret=verify_name(fp,,&peerpub_buf, &peerpub_len,str,hello_buf,hello_len, sign_buf, sign_len, &peernonce)) <= 0) {//modify this
			return -1;
	}
	printf("%s\n%s",my_mail,peer_mail);
	return 1;
}
int decode_incoming_message(int sk,char* in_chat,X509_STORE* str){
	int ret;
	unsigned char* recv_buf;
	unsigned char *hello, *sign, *cert;
	unsigned int hello_len,sign_len,cert_len;
	char format;
	if(*in_chat == 0){
		if( (ret=recv_hello(sk,&hello,&hello_len,&sign,&sign_len,&cert,&cert_len)) < 0){
			return -1;
		}
		//ret = accept_connection(recv_buf,&mynonce,sk,*in_chat,str);
//		printf("Ricevuto HELO_MSG1\n");
		return ret;	
	}
	if((ret=recv_msg(sk,&recv_buf,&format)) == 0){
		printf(" client disconnesso\n");
		return -1;//executes close in main
	}
	if(ret == -1){
		perror("errore sulla receive\n");
		free(recv_buf);
		return 0;//stay connected
	}
	if (format == (char)CHAT_MSG){
			printf("\n");
			write(1,recv_buf,ret);
			free(recv_buf);
			return ret;
	}		
	return -1;//messages with a wrong format, executes close in main
}
int accept_connection(unsigned char* buf,int* nonce,int sk,char* in_chat,X509_STORE* str){
	if(
}
int main(int argc, char*argv[]) {

	socklen_t len;					/* Length of the client address */
	int sk, peer_sk = 0;						/* Passive socket */
	int optval,nonce = 0;						/* Socket options */
	int my_port, peer_port;					/* my port */
	fd_set rfds, rfds_copy;					/* array of fd for the select*/
	int nfds;
	int i, ret;
	uint16_t tmp;
	FILE* fp;
	X509* cert;
	X509_STORE* str;
	short int num_byte;
	unsigned char* buf, *recv_buf;
	char in_chat = 0, ipbuf[DIM_IP];
	char *format;
	struct sockaddr_in my_addr, peer_addr;		/* mine and peer's addresses */
	if((fp=fopen(AUTO_CERT,"r"))==NULL){
		printf("Cannot open the TTP certificate\n");
		return 1;
	}
	if (argc!=2) {
	printf ("Error inserting parameters. Usage: \n\t %s (port) \n\n", argv[0]);
	return 1;
	}
	if((str=X509_STORE_new())==NULL){
		printf("error on creating store for certificate\n");
		return 1;
	}
	if((cert=PEM_read_X509(fp,NULL,NULL,NULL))==NULL){
		printf("Cannot read TTP certificate\n");
		return 1;
	}
	if(X509_STORE_add_cert(str,cert)!=1){
		printf("Error on adding TTP certificate on the store\n");
		return 1;
	}
	FD_ZERO(&rfds);				/* initialize fd_set */
	FD_SET(0,&rfds);			/* wait for stadndard input */
	nfds = 1;
	my_port = atoi(argv[1]);
	if((sk=create_socket(&my_addr,my_port,NULL)) == -1){
		perror("Error on creating socket\n");
		return 1; 
	}else if(ret == -2){
		perror("Bad port or ip\n");
		return 1;
	}
	optval = 1;
	ret = setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	if(ret == -1) {
		printf("\nError setting SO_REUSEADDR\n");
		return 1;
	}
	ret = bind(sk, (SA *) &my_addr, sizeof(my_addr));
	if(ret == -1) {
		printf("\nError binding the socket\n");
		return 1;
	}
	/* Creation of backlog queue */
	ret = listen(sk, BACKLOG_SIZE);
	if(ret == -1) {
		printf("\nError creating the backlog queue, size %d\n", BACKLOG_SIZE);
		return 1;
	}
	
	printf("Waiting for connections ...\n");
	FD_SET(sk,&rfds);
	nfds = sk+1;
	for(;;){
		printf("%c ", in_chat==0? '#':'>');
		fflush(NULL);
		rfds_copy = rfds;
		if( select(nfds,&rfds_copy,NULL,NULL,NULL) == -1 ){
			perror("select error\n");
			return -1;
		}
		for (i=0; i < nfds;i++){
			if(FD_ISSET(i,&rfds_copy)){
				if(i == sk){
					printf("tentativo di connessione\n");
					len = sizeof(SA);
					if((peer_sk = accept(sk, (SA*)&peer_addr,&len))==-1){
						perror ("error on accept\n");
						return -1;
					}
					FD_SET(peer_sk,&rfds);
					if (peer_sk >= nfds){
						nfds = peer_sk+1;
					}
					//in_chat = 1;
				}	
				else if (i == 0){
					buf = (unsigned char*)calloc(1,DIM_BUF);
					num_byte = read(0,buf,DIM_BUF);
					if (in_chat == 0){
						if(buf[0] == '!'){
							switch (buf[1]){
							case 'c':
								if((ret = init_connection(&buf[2],&peer_sk,nonce++,str)) == -1){
									perror("error on initialization\n");
									goto close;
								}else if(ret == -2){
									perror("Bad port or ip\n");
									continue;
								}
								FD_SET(peer_sk,&rfds);
								if (peer_sk >= nfds){
									nfds = peer_sk+1;
								}
								in_chat = 1;
								break;
							case 'q':
								goto close_all;	
							}
						}
					}else {
						if (buf[0] == '!'){
							if(buf[1] == 'q'){
								goto close;
							} else{
								printf("Comando non valido\n");
							}
						} else{
							if(send_msg(peer_sk,buf,num_byte,CHAT_MSG) < 0){
								perror("errore sulla send\n");
								continue;
							}
						}	
					}
				} else{
				
						if(decode_incoming_message(peer_sk,&in_chat,str) < 0){
							perror("error on decoding incoming message\n");
							goto close;
						}
				/*	if( (ret=recv_msg(i,&recv_buf,&format)) == 0){
						printf(" client disconnesso\n");
						goto close;
					} else if(ret == -1){
						perror("errore sulla receive\n");
						continue;
					}
					printf("\n");
					write(1,recv_buf,ret);
					free(recv_buf);*/
				}
			}
		}
		continue;
		close:
			close(peer_sk);
			FD_CLR(peer_sk,&rfds);
			peer_sk = 0;
			in_chat = 0;
	}
	close_all:
		if(peer_sk != 0){	
			close(peer_sk);
		}
		close(sk);
}	