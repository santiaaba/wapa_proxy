#include "parce.h"

void parce_data(char *buffer, int *i, char *value){
	/* Dado un comando recibido desde el servidor con formato
	   de campos separados por |, retorna el valor a partir
	   de la posición dada hasta el proximo | o fin del string.
	   Retorna ademas la posición fin en i. los datos comienzan
	   en la posicion 3*/

	int j = 0;
	int largo;

	largo = strlen(buffer);
	while(*i < largo && buffer[*i] != '|' && buffer[*i] != '\0'){
		value[j] = buffer[*i];
		j++; (*i)++;
	}
	value[j]='\0';
	/* Adelantamos la i una posicion ya que se
	   encuentra parada en el caracter "|" posiblemente */
	(*i)++;
}
char parce_get_action(char *buffer){
	/* Dado el buffer retorna la accion a realizar. El mismo
	   es el char en la posicion 2 */
	return buffer[4];
}
void parce_get_taskid(char *buffer, char *value){
	/* Dado un buffer, retorna el taskid como
	   una secuencia de 2 bytes (dos char). Se
	   encuentran en la posición 0 a la 1. */
	value[0] = buffer[0];
	value[1] = buffer[1];
	value[2] = buffer[2];
	value[3] = buffer[3];
}
