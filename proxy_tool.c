#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "parce.h"
#include "config.h"

#define PORT 3550 /* El puerto que será abierto */
#define BACKLOG 1 /* El número de conexiones permitidas */
#define SITESD "/etc/nginx/sites.d/"
#define BUFFER_SIZE 1024

/********************************
*      FUNCIONES	       *
********************************/

void clear_buffer(char *buffer_rx){
	int i;
	for (i=0;i<BUFFER_SIZE;i++){
		buffer_rx[i] = '\0';
	}
}

char nginx_systemctl(int action){	//0 stop, 1 start, 2 reload
	FILE *fp;
	char status[2];

	switch(action){
		case 0:
			fp = popen("systemctl stop nginx 2>/dev/null; echo $?", "r");
			break;
		case 1:
			fp = popen("systemctl start nginx 2>/dev/null; echo $?", "r");
			break;
		case 2:
			fp = popen("systemctl reload nginx 2>/dev/null; echo $?", "r");
			break;
		default:
			pclose(fp);
			return 0;
	}
	fgets(status, sizeof(status)-1, fp);
	pclose(fp);
	return atoi(status);
}

int add_site(char *buffer_rx, int fd_client, T_config *c){
	/* Agrega un sitio a la configuracion del NGINX */
	char site_name[100];
	char aux[500];
	int pos=2;
	char alias[200];
	FILE *fd;
	char site_path[100];
	char buffer_tx[BUFFER_SIZE];


	
	parce_data(buffer_rx,&pos,site_name);

	sprintf(site_path,"%s%s.conf",SITESD,site_name);
	printf("Abriendo archivo: %s\n",site_path);
	fd = fopen(site_path,"w");
	if(!fd){
		printf("Problemas para escribir el archivo %s\n",site_path);
		send(fd_client,"0", BUFFER_SIZE,0);
		return 0;
	}

	parce_data(buffer_rx,&pos,aux); //site_id
	fprintf(fd,"#ID %s\n",aux);
	parce_data(buffer_rx,&pos,aux); //site_ver
	fprintf(fd,"#VER %s\n",aux);

	printf("PASO1\n");
	/* Indicamos que estamos listos para recibir los workers */
	/* Ojo que con esto eliminamos el buffer_rx anterior */
	fprintf(fd,"upstream %s {\n",site_name);
	do{
		send(fd_client,"1", BUFFER_SIZE,0);
		recv(fd_client,buffer_rx,BUFFER_SIZE,0);
		pos=2;
		while(pos < strlen(buffer_rx)){
			parce_data(buffer_rx,&pos,aux);
			fprintf(fd,"\tserver %s;\n",aux);
		}
		printf("buffer_rx -%s\n-",buffer_rx);
	} while(buffer_rx[0] != '0');
	printf("PASO2\n");

	fprintf(fd,"}\n");
	fprintf(fd,"server {\n");
	fprintf(fd,"\tlisten 80;\n");
	fprintf(fd,"\tserver_name %s.%s;\n",site_name,config_default_domain(c));

	/* Aca van los server name/alias */
	printf("PASO3\n");
	do{
		send(fd_client,"1", BUFFER_SIZE,0);
		recv(fd_client,buffer_rx,BUFFER_SIZE,0);
		pos=2;
		while(pos < strlen(buffer_rx)){
			parce_data(buffer_rx,&pos,aux);
			fprintf(fd,"\tserver_name %s;\n",aux);
		}
		printf("buffer_rx -%s\n-",buffer_rx);
	} while(buffer_rx[0] != '0');
	send(fd_client,"1", BUFFER_SIZE,0);
	printf("PASO4\n");

	fprintf(fd,"\tlocation / {\n");
	fprintf(fd,"\t\tproxy_pass http://%s;\n",site_name);
	fprintf(fd,"\t\tproxy_set_header X-Real-IP $remote_addr;\n");
	fprintf(fd,"\t\tproxy_set_header Host $http_host;\n");
	fprintf(fd,"\t}\n");
	fprintf(fd,"}\n");
	
	printf("PASO5\n");
	fclose(fd);
	return 1;
}

void delete_site(char *buffer_rx, int fd_client){
	/* Elimina el archivo de configuracion de un sitio */
	FILE *fp;
	char command[1000];
	char aux[100];
	int pos;

	pos=2;
	parce_data(buffer_rx,&pos,aux);
	sprintf(command,"rm %s%s.conf -f",SITESD,aux);
	printf(command);
	fp = popen(command, "r");
	pclose(fp);
	send(fd_client,"1", BUFFER_SIZE,0);
}

void delete_all(char *buffer_rx, int fd_client){
	/* Elimina la configuracion de sitios toda */
	FILE *fp;
        char command[1000];

	sprintf(command,"rm %s* -f",SITESD);
	printf(command);
	fp = popen(command, "r");
	pclose(fp);
	send(fd_client,"1", BUFFER_SIZE,0);
}

int check(char *detalle){
	FILE *fp;
        char buffer[100];
        int status = 1;

	strcpy(detalle,"todo OK"); // De entrada esta todo bien
	fp = popen("systemctl status nginx > /dev/null; echo $?", "r");
	if (fp == NULL) {
		printf("Fallo al obtener el estado del nginx\n" );
		status = 0;
	}
	fgets(buffer, sizeof(buffer)-1, fp);
	pclose(fp);

	if(buffer[0] != '0'){
		strcpy(detalle,"nginx caido");
		status = 0;
	}
	return status;
}

void statistics(char *aux){
	/* Obtiene estadisticas del worker */
	FILE *fp;
	char buffer[100];

	/* Para la CPU */
	fp = popen("uptime | awk '{print \"|\"$10\"|\"$11\"|\"$12\"|\"}' | sed 's\\,\\\\g'", "r");
	fgets(buffer, sizeof(buffer)-1, fp);
	strcpy(aux,buffer);
	aux[strlen(aux) - 1] = '\0';
	pclose(fp);

	/* Para la CPU */
	fp = popen("vmstat | tail -1 | awk '{print $14\"|\"$13\"|\"$15\"|\"$16\"|\"}'", "r");
	fgets(buffer, sizeof(buffer)-1, fp);
	strcat(aux,buffer);
	aux[strlen(aux) - 1] = '\0';
	pclose(fp);

	/* Para la memoria */
	fp = popen("free | tail -2 | head -1 | awk '{print $2\"|\"$3\"|\"}'", "r");
	fgets(buffer, sizeof(buffer)-1, fp);
	strcat(aux,buffer);
	aux[strlen(aux) - 1] = '\0';
	pclose(fp);

	/* Para la swap */
	fp = popen("free | tail -1 | awk '{print $2\"|\"$3}'", "r");
	fgets(buffer, sizeof(buffer)-1, fp);
	strcat(aux,buffer);
	aux[strlen(aux) - 1] = '\0';
	pclose(fp);
}

int repare(){
	/* intenta reparar el worker */
	/* retorna 1 si tuvo exito. 0 en caso contrario */
	return 1;
}

/********************************
 *      MAIN		    *
 ********************************/

int main(int argc , char *argv[]){

	int fd_server, fd_client; /* los ficheros descriptores */
	char buffer_rx[BUFFER_SIZE];
	char buffer_tx[BUFFER_SIZE];
	struct sockaddr_in server;
	struct sockaddr_in client;
	T_config config;
	int sin_size;
	int pos;
	char aux[200];
	char action;
	int result;
	int cant_bytes;

	config_load("proxy_tool.conf",&config);
	
	if ((fd_server=socket(AF_INET, SOCK_STREAM, 0)) == -1 ) {
		printf("error en socket()\n");
		return 1;
	}
	server.sin_family = AF_INET;
	server.sin_port = htons(PORT);
	server.sin_addr.s_addr = INADDR_ANY;

	if(bind(fd_server,(struct sockaddr*)&server, sizeof(struct sockaddr))<0) {
		printf("error en bind() \n");
		return 1;
	}

	if(listen(fd_server,BACKLOG) == -1) {  /* llamada a listen() */
		printf("error en listen()\n");
		return 1;
	}
	sin_size=sizeof(struct sockaddr_in);

	while(1){
		printf("Esperando conneccion desde el cliente()\n"); //Debemos mantener viva la conexion
		if ((fd_client = accept(fd_server,(struct sockaddr *)&client,&sin_size))<0) {
			printf("error en accept()\n");
			return 1;
		}

		// Aguardamos continuamente que el cliente envie un comando
		while(recv(fd_client,buffer_rx,BUFFER_SIZE,0)>0){
			printf("Esperando conneccion desde el cliente()\n"); //Debemos mantener viva la conexion
			printf("Recibimos -%s-\n",buffer_rx);
			action = buffer_rx[0];
			switch(action){
				case 'A':
					printf("Agregamos sitio\n");
					add_site(buffer_rx,fd_client,&config);
					break;
				case 'd':
					delete_site(buffer_rx,fd_client);
					break;
				case 'D':
					delete_all(buffer_rx,fd_client);
					break;
				case 'C':
					printf("Chequeamos el worker\n");
					if(check(aux) == 1){
						buffer_tx[0] = '1';
					} else {
						buffer_tx[0] = '0';
					}
					buffer_tx[1] = '|'; buffer_tx[2] = '\0';
					strcat(buffer_tx,aux);
					statistics(aux);
					strcat(buffer_tx,aux);

					printf("Esperando que el cliente reciba los datos\n");
					cant_bytes = send(fd_client,buffer_tx, BUFFER_SIZE,0);
					printf("Enviamos(%i) -%s-\n",cant_bytes,buffer_tx);
					break;
				case 'S':
					/* Start Nginx */
					if(nginx_systemctl(0) == 0){
						buffer_tx[0] = 1;
					} else {
						buffer_tx[0] = 0;
						}
					buffer_tx[1] = '|'; buffer_tx[2] = '\0';
					send(fd_client,buffer_tx, BUFFER_SIZE,0);
					break;
				case 'K':
					/* Stop Nginx */
					if(nginx_systemctl(1) == 0){
						buffer_tx[0] = 1;
					} else {
						buffer_tx[0] = 0;
						}
					buffer_tx[1] = '|'; buffer_tx[2] = '\0';
					send(fd_client,buffer_tx, BUFFER_SIZE,0);
					break;
				case 'R':
					/* Reload Nginx */
					if(nginx_systemctl(2) == 0){
						buffer_tx[0] = 1;
					} else {
						buffer_tx[0] = 0;
						}
					buffer_tx[1] = '|'; buffer_tx[2] = '\0';
					send(fd_client,buffer_tx, BUFFER_SIZE,0);
					break;
				default :
					printf("Error protocolo\n");
					send(fd_client,"0\0",BUFFER_SIZE,0);
			}
		}
	}
	close(fd_client);
}
