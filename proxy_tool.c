#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "parce.h"
#include "send_receive.h"
#include "config.h"

#define SITESD "/etc/nginx/sites.d/"

/********************************
*      FUNCIONES	       *
********************************/

void nginx_systemctl(int action, char **send_message, uint32_t *send_message_size){	//0 stop, 1 start, 2 reload
	FILE *fp;
	char status[2];

	*send_message_size=2;
	*send_message=(char *)realloc(*send_message,*send_message_size);
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
			strcpy(*send_message,"0");
	}
	fgets(status, sizeof(status)-1, fp);
	pclose(fp);
	strcpy(*send_message,"1");
}

int add_site(char *rcv_message, char **send_message, uint32_t *send_message_size, T_config *cfg){
	/* Agrega un sitio a la configuracion del NGINX */
	char site_name[100];
	char aux[500];
	char alias[200];
	FILE *fd;
	char site_path[100];
	char *aux_list = NULL;
	int aux_list_size;
	int pos = 1;
	int posaux;

	parce_data(rcv_message,'|',&pos,site_name);
	sprintf(site_path,"%s%s.conf",SITESD,site_name);
	printf("Abriendo archivo: %s\n",site_path);
	if(fd = fopen(site_path,"w")){
		parce_data(rcv_message,'|',&pos,aux); //site_id
		fprintf(fd,"#ID %s\n",aux);
		parce_data(rcv_message,'|',&pos,aux); //site_ver
		fprintf(fd,"#VER %s\n",aux);
	
		printf("PASO1\n");
		/* Indicamos que estamos listos para recibir los workers */
		/* Ojo que con esto eliminamos el rcv_message anterior */
		aux_list = (char *)malloc(strlen(rcv_message) - pos + 1);
		parce_data(rcv_message,'|',&pos,aux_list);
		aux_list_size = strlen(aux_list);
		posaux=0;
		fprintf(fd,"upstream %s {\n",site_name);
		while(posaux < aux_list_size){
			parce_data(aux_list,',',&posaux,aux);
			fprintf(fd,"\tserver %s;\n",aux);
		}
		fprintf(fd,"}\n");
		fprintf(fd,"server {\n");
		fprintf(fd,"\tlisten 80;\n");
		fprintf(fd,"\tserver_name %s.%s;\n",site_name,config_default_domain(cfg));
	
		/* Aca van los server name/alias */
		aux_list = (char *)malloc(strlen(rcv_message) - pos + 1);
		parce_data(rcv_message,'|',&pos,aux_list);
		aux_list_size = strlen(aux_list);
		posaux=0;
		while(posaux<aux_list_size){
			parce_data(aux_list,',',&posaux,aux);
			fprintf(fd,"\tserver_name %s;\n",aux);
		}
	
		fprintf(fd,"\tlocation / {\n");
		fprintf(fd,"\t\tproxy_pass http://%s;\n",site_name);
		fprintf(fd,"\t\tproxy_set_header X-Real-IP $remote_addr;\n");
		fprintf(fd,"\t\tproxy_set_header Host $http_host;\n");
		fprintf(fd,"\t}\n");
		fprintf(fd,"}\n");
	
		fclose(fd);
		sprintf(*send_message,"1");
	} else {
		sprintf(*send_message,"0");
	}
}

void delete_site(char *rcv_message, char **send_message, uint32_t *send_message_size){
	/* Elimina el archivo de configuracion de un sitio */
	FILE *fp;
	char command[1000];
	char aux[100];

	int pos=1;
	parce_data(rcv_message,'|',&pos,aux);
	sprintf(command,"rm %s%s.conf -f",SITESD,aux);
	printf(command);
	fp = popen(command, "r");
	pclose(fp);
	*send_message_size = 2;
        *send_message = (char *)realloc(*send_message,*send_message_size);
	sprintf(*send_message,"1");
}

void delete_all(char *rcv_message, char **send_message, uint32_t *send_message_size){
	/* Elimina la configuracion de sitios toda */
	FILE *fp;
	char command[100];

	sprintf(command,"rm %s* -f",SITESD);
	printf(command);
	fp = popen(command, "r");
	pclose(fp);
	*send_message_size = 2;
        *send_message = (char *)realloc(*send_message,*send_message_size);
	sprintf(*send_message,"1");
}

void check(char **send_message, uint32_t *send_message_size){
	FILE *fp;
	char buffer[100];
	char detalle[300];
	char aux[200];
	char status = '1';

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
		status = '0';
	}
	*send_message_size = strlen(detalle) + 3; //el 3 incluye el estado , el "|" y el \0
	*send_message=(char *)realloc(*send_message,*send_message_size);
	sprintf(*send_message,"%c%s|",status,detalle);

	/* Para la CPU */
	fp = popen("uptime | awk '{print $10\"|\"$11\"|\"$12\"|\"}' | sed 's\\,\\\\g'", "r");
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

	*send_message_size += strlen(aux);
	*send_message=(char *)realloc(*send_message,*send_message_size);
	strcat(*send_message,aux);
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
	struct sockaddr_in server;
	struct sockaddr_in client;
	char *rcv_message=NULL;
	int rcv_message_size;
	char *send_message=NULL;
	int send_message_size;

	T_config cfg;
	int sin_size;
	int pos;
	char aux[200];
	char action;
	int result;
	int cant_bytes;

	config_load("proxy_tool.conf",&cfg);
	
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
		while(recv_all_message(fd_client,&rcv_message,&rcv_message_size)){
			printf("Esperando conneccion desde el cliente()\n"); //Debemos mantener viva la conexion
			printf("Recibimos -%s-\n",rcv_message);
			action = rcv_message[0];
			switch(action){
				case 'A':
					printf("Agregamos sitio\n");
					add_site(rcv_message,&send_message,&send_message_size,&cfg);
					break;
				case 'd':
					delete_site(rcv_message,&send_message,&send_message_size);
					break;
				case 'D':
					delete_all(rcv_message,&send_message,&send_message_size);
					break;
				case 'C':
					printf("Chequeamos el worker\n");
					check(&send_message,&send_message_size);
					break;
				case 'S':
					/* Start Nginx */
					nginx_systemctl(0,&send_message,&send_message_size);
					break;
				case 'K':
					/* Stop Nginx */
					nginx_systemctl(1,&send_message,&send_message_size);
					break;
				case 'R':
					/* Reload Nginx */
					nginx_systemctl(2,&send_message,&send_message_size);
					break;
				default :
					printf("Error protocolo\n");
					send_message_size=2;
					send_message = (char *)realloc(send_message,send_message_size);
					sprintf(send_message,"0");
			}
			send_all_message(fd_client,send_message,send_message_size);
		}
		close(fd_client);
	}
}
