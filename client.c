#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <setjmp.h>

#define MAX_USERNAME 1024
#define MAX_PASSWORD 1024
#define MAX_INPUT_USER 1024
#define MAX_OUTPUT_SERVER 1024
#define DIM_OUTPUT_SERVER 3  // OK\0  NO\0 
#define DIM_INPUT_SERVER 7 // /LOGIN\0 /SIGIN\0
#define SRV_PORT 4242
#define MAX_CLIENT_CHAT 10
#define MAX_MSG 1024
#define MAX_CRONOLOGIA 4096
#define TIME_LENGTH 19   //yyyy:mm:gg:hh:mm:ss
#define MAX_RUBRICA 4096
#define MAX_PATH 50
#define MAX_BUFFER_FILE 50000


// comunicazioni col server e coi client
char REQUS[]="/REQUS\0"; // richiesta ip/porta user
char USRCO[]="/USRCO\0"; // user connesso
char USRNE[]="/USRNE\0"; // user non esistente
char USRDS[]="/USRDS\0"; // user disconnesso
char LOGIN[]="/LOGIN\0"; // richiesta di login
char SIGIN[]="/SIGIN\0"; // richiesta di signin
char ACKRE[]="/ACKRE\0"; // richiesta di ack
char CHREQ[]="/CHREQ\0"; // richiesta di chat
char CONRE[]="/CONRE\0"; // richiesta di chat refused
char CONAC[]="/CONAC\0"; // richiesta di chat accepted
char SERDO[]="/SERDO\0"; // server down
char USDOW[]="/USDOW\0"; // user è andato down
char CHCLS[]="/CHCLS\0"; // chat closed
char CHSRV[]="/CHSRV\0"; // notifico al server che vado in chat con lui perchè l'utente è disconnesso
char CHSRC[]="/CHSRC\0"; // notifico al server che ho chiuso la chat con lui
char CHHNG[]="/CHHNG\0"; // richiesta dei messaggi pendenti
char SHHNG[]="/SHHNG\0"; // show hanging
char NOTIF[]="/NOTIF\0"; // notifica che lo user ha letto i miei messaggi
char NOTSI[]="/NOTSI\0"; // l'user che ha fatto login ha dei messaggi pendenti
char NOTNO[]="/NOTNO\0"; // non ne ha

// spunte
char UNA_SPUNTA[]="[*]\0";
char DUE_SPUNTE[]="[**]\0";

// variabili di utilità globali
int CHAT_ON;    // flag che indica se sia in chat con altri utenti
int CHAT_SERVER; // flag che indica se sia in chat col server
fd_set MASTER;
int FDMAX;
char USERNAME [MAX_USERNAME];
uint16_t MY_PORT_T;
uint32_t MY_IP_T;
int SOCKET_ASCOLTO;
int SOCKET_COMUNICAZIONE_SERVER;
uint16_t PORTA_CLIENT_AGGIUNTO;
uint32_t IP_CLIENT_AGGIUNTO;
int RETURN_VALUE_CLIENT;
int FLAG_SERVER_DOWN;
int FLAG_USCITA_IMPROVVISA;
char PATH [MAX_USERNAME];
char MYSELF[MAX_USERNAME];

// variabili processo padre
pid_t PROCESSO_PADRE;

// VARIABILI DEI THREAD
pthread_t THREAD_COMUNICAZIONI_SERVER;
pthread_t THREAD_FIGLIO_RICEZIONE;

// VARIABILI DI SINCRONIZZAZIONE
pthread_cond_t RISPOSTA_ARRIVATA;
pthread_mutex_t MUTEX;

pthread_mutex_t MUTEX_FILE;

struct GROUP{
	int NUM_CLIENT_GROUP;
	uint32_t IP_T[MAX_CLIENT_CHAT];
	uint16_t PORT_T[MAX_CLIENT_CHAT];
	int sd[MAX_CLIENT_CHAT];
};
struct GROUP GRUPPO_CHAT;

jmp_buf buf;

struct timeval timeout={1,0};

////////////////// FUNZIONI DI UTILITÀ ////////////////////////
void handler_close(int sig){
	close(SOCKET_COMUNICAZIONE_SERVER);
	close(SOCKET_ASCOLTO);
	exit(1);
}

void scomposizione_msg(char*msg, char*contenuto_msg){
	int i,j,k;
	k=0;
	for(i=0; msg[i]!=':'; i++);
	// arrivo qua e msg[i]=:
	for(j=i+2; msg[j]!='\0'; j++){
		contenuto_msg[k]=msg[j];
		k++;
	}
	contenuto_msg[k]='\0';
}

void mostra_rubrica(){
	FILE* rubrica;
	int n;
	int i,k=0;
	char path_rubrica[MAX_USERNAME];
	char content_rubrica[MAX_RUBRICA];
	char nome[MAX_USERNAME];
	sprintf(path_rubrica,"rubrica/%s.txt", MYSELF);
	rubrica=fopen(path_rubrica, "r");
	n=fread(content_rubrica, sizeof(char), sizeof(content_rubrica), rubrica);
	fclose(rubrica);
	printf("[UTENTI RUBRICA]\n");
	for(i=0; i<n; i++){
		if(content_rubrica[i]==' '){
			nome[k]='\0';
			printf("%s\n", nome);
			k=0;
			continue;
		}
		nome[k]=content_rubrica[i];
		k++;
	}
	nome[k]='\0';
	printf("%s\n", nome);
}

void save_msg_cronologia(char*msg){
	FILE* cronologia_chat;

	pthread_mutex_lock(&MUTEX_FILE);

	// in path avrò scritto lisamarco.txt
	cronologia_chat=fopen(PATH,"a");
	fwrite(msg, sizeof(char), strlen(msg)+1, cronologia_chat);
	fclose(cronologia_chat);

	pthread_mutex_unlock(&MUTEX_FILE);
}

void receive_share(int sd){
	FILE* fd;
	int ricevuti;
	int ricevuti_dopo;
	char buffer[MAX_BUFFER_FILE];
	uint16_t dim;
	uint16_t dim_path;
	char path[MAX_PATH];
	char myusr[MAX_USERNAME];
	strcpy(myusr, MYSELF);

	// ricevo il nome del file
	recv(sd, (void*)&dim_path, sizeof(uint16_t), MSG_WAITALL);
	dim_path=ntohs(dim_path);
	recv(sd, (void*)path, dim_path, MSG_WAITALL);
	
	// ricevo la dimensione del file
	ricevuti=recv(sd, (void*)&dim, sizeof(uint16_t), MSG_WAITALL);
	dim=ntohs(dim);
	if(dim>MAX_BUFFER_FILE){
		printf("dim > buffer\n");
	}
	
	ricevuti=recv(sd, (void*)buffer, dim, MSG_WAITALL);
	while(ricevuti<dim){
		ricevuti_dopo=recv(sd, (void*)buffer, dim-ricevuti, MSG_WAITALL);
		ricevuti+=ricevuti_dopo;
	}
	
	// a questo punto in buffer ho tutto
	strcat(myusr, path);
	fd=fopen(myusr, "w");
	fwrite(buffer, sizeof(char), dim, fd);
	printf("[FILE %s RICEVUTO]\n", path);
}

void* thread_receive(void* arg){
	// funzione che si occupa di ricevere i messaggi quando sono in chat
	
	// variabili di utilità funzione
	fd_set readwrite_fds;
	int i, dim_dati,len,j,k,ret;
	char msg[MAX_MSG];
	char msg_contenuto[MAX_MSG];

	// variabili del nuovo client
	int new_client_sd;
	struct sockaddr_in new_indirizzo_client;
	uint16_t porta_ascolto_nc;

	// variabili del client da cancellare
	uint32_t ip_to_del;
	uint16_t porta_to_del;

	while(1){
		readwrite_fds=MASTER;
		// mi serve di mettere un timeout perchè se qualcuno è stato aggiunto e parla per primo, allora i suoi messaggi non verranno ricevuti perchè non è ancora stato inserito in read fds
		select(FDMAX+1, &readwrite_fds, NULL, NULL, &timeout);
		for(i=0; i<=FDMAX; i++){
			if(FD_ISSET(i,&readwrite_fds)){
				dim_dati=ricezione_dim_dati(i);
				ret=recv(i,(void*)msg,dim_dati,MSG_WAITALL);
				if(ret==-1){
					perror("[ERORE RECEIVE THREAD RECEIVE: ");
					exit(1);
				}
				if(ret==0){
					// devo far uscire il padre da gestore chat e farlo andare in show_menu
					printf("[UN CLIENT SI È DISCONNESSO IMPROVVISAMENTE. PREMERE UN TASTO + INVIO PER USCIRE DALLA CHAT]\n");
					FLAG_USCITA_IMPROVVISA=1;
					pthread_exit(NULL);
					return;
				}
				
				// il messaggio ricevuto potrebbe essere la richiesta che qualcuno sta aggiungendo un client alla chat
				// però è formattato del tipo: username:msg
				// devo ottenere msg
				scomposizione_msg(msg,msg_contenuto);
				if(strcmp("/a",msg_contenuto)==0){
					// qualcuno sta aggiungendo un client
					// lo inserisco nel set dei client da monitorare
					len=sizeof(new_indirizzo_client);
					new_client_sd=accept(SOCKET_ASCOLTO, (struct sockaddr*)&new_indirizzo_client, &len);
					FD_SET(new_client_sd, &MASTER);
					if(new_client_sd>FDMAX){
						FDMAX=new_client_sd;
					}
					// devo farmi dare la porta del suo socket di ascolto nel caso in cui voglia aggiungere qualcuno
					recv(new_client_sd, (void*)&porta_ascolto_nc, sizeof(uint16_t), MSG_WAITALL);

					// aggiorno i dati del gruppo di cui faccio parte
					GRUPPO_CHAT.IP_T[GRUPPO_CHAT.NUM_CLIENT_GROUP]=htonl(new_indirizzo_client.sin_addr.s_addr);
					GRUPPO_CHAT.PORT_T[GRUPPO_CHAT.NUM_CLIENT_GROUP]=porta_ascolto_nc;
					GRUPPO_CHAT.sd[GRUPPO_CHAT.NUM_CLIENT_GROUP]=new_client_sd;
					GRUPPO_CHAT.NUM_CLIENT_GROUP++;
					printf("[UN UTENTE SI STA AGGIUNGENDO ALLA CHAT..]\n");
					continue;
				}
				if(strcmp("/share", msg_contenuto)==0){
					receive_share(i);
					continue;
				}
				save_msg_cronologia(msg);
				printf("%s\n", msg);
			}
		}
	}
}

// serve per inviare anticipatamente al server la dimensione dei dati che verranno inviati
void invio_dim_dati(int sd, int dim){
	uint16_t lmsg;
	int ret;
	lmsg=htons(dim);
	ret=send(sd, (void*)&lmsg, sizeof(uint16_t),0);
	if(ret==-1){
		perror("Errore nell'invio della dimensione dei dati");
		exit(1);
	}
}

int ricezione_dim_dati(int sd){
	uint16_t lmsg;
	recv(sd, (void*)&lmsg, sizeof(uint16_t),MSG_WAITALL);
	lmsg=ntohs(lmsg);
	return (int)lmsg;
}

// server per farmi dare ip e porta dato l'username per cominciare una nuova chat
// ritorna -1 se lo username destinatario non esiste
// ritorna 0 se è andato a buon fine
// ritorna 1 se lo username è disconnesso
void richiesta_ip_porta(int sd, char*username){
	char risposta_server[MAX_OUTPUT_SERVER];
	int ret;

	send(sd, (void*)REQUS, DIM_INPUT_SERVER, 0);
	invio_dim_dati(sd, strlen(username)+1);
	send(sd, (void*)username, strlen(username)+1, 0);
	
	// la risposta mi arriverà dal thread di comunicazione col server che mette tutto nelle variabili globali
	// devo bloccare il master fino a che il thread di receive non ha finito con la comunicazione con l'altro client
	pthread_cond_wait(&RISPOSTA_ARRIVATA, &MUTEX);
}

void chat_client_disconnesso(char* username_to_contact){
	// parlo solo col server
	// variabili di utilità
	char msg[MAX_MSG];
	char msg_to_send[MAX_MSG];
	int len;
	// messaggio del tipo: 
	//username_to_contact.msg

	printf("[L'UTENTE NON È RAGGIUNGIBILE. I MESSAGGI VERRANNO SALVATI SUL SERVER E RECAPITATI UNA VOLTA CHE L'UTENTE TORNERÀ ONLINE]\n");
	printf("Scrivere '/q'+INVIO per uscire dalla chat\n");
	len=strlen(username_to_contact);
	username_to_contact[len]='.';
	username_to_contact[len+1]='\0';
	while(1){
		scanf("%s", msg);
		if(strcmp("/q", msg)==0){
			send(SOCKET_COMUNICAZIONE_SERVER, (void*)CHSRC, DIM_INPUT_SERVER, 0);
			printf("[CHIUSURA CHAT]\n");
			return;
		}
		save_msg_cronologia(UNA_SPUNTA);
		save_msg_cronologia(msg);
		send(SOCKET_COMUNICAZIONE_SERVER, (void*)CHSRV, DIM_INPUT_SERVER, 0);
		
		// formattazione del messaggo stile: username_to_contact.USERNAME: msg
		strcpy(msg_to_send, username_to_contact);
		strcat(msg_to_send, USERNAME);
		strcat(msg_to_send, msg);
		
		// invio del messaggio
		invio_dim_dati(SOCKET_COMUNICAZIONE_SERVER, strlen(msg_to_send)+1);
		send(SOCKET_COMUNICAZIONE_SERVER, (void*)msg_to_send, strlen(msg_to_send)+1, 0);
	}
}

void exit_chat_client(){
	int i;
	int ret;
	// devo killare il processo delle receive
	pthread_cancel(THREAD_FIGLIO_RICEZIONE);
	// CHIUDO I SOCKET 
	for(i=0; i<GRUPPO_CHAT.NUM_CLIENT_GROUP; i++){
		ret=close(GRUPPO_CHAT.sd[i]);
		if(ret==-1){
			perror("ERRORE CLOSE exit_chat_client: ");
			exit(1);
		}
	}
	// mando un messaggio al server
	send(SOCKET_COMUNICAZIONE_SERVER, (void*)CHCLS, DIM_INPUT_SERVER, 0);
	CHAT_ON=0;
	FLAG_USCITA_IMPROVVISA=0;
	return;
}

void print_cronologia(){
	char cronologia[MAX_CRONOLOGIA];
	char msg[MAX_MSG];
	FILE* cronologia_chat;
	int c;
	int n;
	int i,j=0;

	pthread_mutex_lock(&MUTEX_FILE);
	printf("[CRONOLOGIA]\n");
	// in path avrò scritto lisamarco.txt
	cronologia_chat=fopen(PATH,"r");
	if(cronologia_chat==NULL){
		// significa che il file non esiste perchè non ho ancora mai chattato con lui
		pthread_mutex_unlock(&MUTEX_FILE);
		return;
	}
	// altrimenti printo tutto quello che c'è dentro
	while(!feof(cronologia_chat)){
		n=fread(cronologia, sizeof(char), sizeof(cronologia), cronologia_chat);
		for(i=0; i<n; i++){
			if(cronologia[i]!='\0'){
				msg[j]=cronologia[i];
				j++;
			}
			if(cronologia[i]=='\0'){
				msg[j]='\0';
				printf("%s\n", msg);
				j=0;
			}
		}
	}
	fclose(cronologia_chat);

	pthread_mutex_unlock(&MUTEX_FILE);
}

void login(char* username){
	char password[MAX_PASSWORD];
	int ret;
	char output_server[DIM_OUTPUT_SERVER];
	char msg_server[DIM_INPUT_SERVER];
	int var_uscita_while=0;

	while(var_uscita_while==0){
		// dico al server che lo contatto per un login
		send(SOCKET_COMUNICAZIONE_SERVER, (void*)LOGIN, strlen(LOGIN)+1, 0);
		// richiedo all'utente username e password
		printf("Inserisci un username \n");
		scanf("%s", username);
		printf("Inserisci una password\n");
		scanf("%s", password);
		// MODIFICA: EFFETTUARE IL CONTROLLO PASSWORD DOPPIO
		invio_dim_dati(SOCKET_COMUNICAZIONE_SERVER, strlen(username)+1);
		ret=send(SOCKET_COMUNICAZIONE_SERVER, (void*)username, strlen(username)+1, 0);
		if(ret==-1){
			perror("Errore invio username");
			exit(1);
		}
		invio_dim_dati(SOCKET_COMUNICAZIONE_SERVER, strlen(password)+1);
		ret=send(SOCKET_COMUNICAZIONE_SERVER, (void*)password, strlen(password)+1, 0);
		if(ret==-1){
			perror("Errore invio password");
			exit(1);
		}
		// RISPOSTA DEL SERVER
		ret=recv(SOCKET_COMUNICAZIONE_SERVER, (void*)output_server, DIM_OUTPUT_SERVER, MSG_WAITALL);
		if(ret==-1){
			perror("Errore ricezione conferma server");
			exit(1);
		}
		if(strcmp("OK", output_server)==0){
			printf("LOGIN AVVENUTO CON SUCCESSO!\n");
			var_uscita_while=1;
			recv(SOCKET_COMUNICAZIONE_SERVER, (void*)msg_server, DIM_INPUT_SERVER, MSG_WAITALL);
			if(strcmp(msg_server, NOTSI)==0){
				printf("[** HAI DELLE NUOVE NOTIFICHE **]\n");
			}
			break;
		}
		else{
			printf("LOGIN FALLITO: USERNAME O PASSWORD ERRATI. Reinserire i dati\n");
		}
	}
}

void sigin(int porta, char* username){
	char password[MAX_PASSWORD];
	int ret;
	char output_server[DIM_OUTPUT_SERVER];
	int var_uscita_while=0;
	uint16_t porta_t;
	uint32_t ip_t;
	int ip;

	// comunico al server che lo contatto per una richiesta di sign in
	while(var_uscita_while==0){
		send(SOCKET_COMUNICAZIONE_SERVER, (void*)SIGIN, strlen(SIGIN)+1, 0);
		// richiedo all'utente username e password
		printf("Inserisci un username \n");
		scanf("%s", username);
		printf("Inserisci una password\n");
		scanf("%s", password);
		// MODIFICA: EFFETTUARE IL CONTROLLO PASSWORD DOPPIO
		invio_dim_dati(SOCKET_COMUNICAZIONE_SERVER, strlen(username)+1);
		ret=send(SOCKET_COMUNICAZIONE_SERVER, (void*)username, strlen(username)+1, 0);
		if(ret==-1){
			perror("Errore invio username");
			exit(1);
		}
		invio_dim_dati(SOCKET_COMUNICAZIONE_SERVER, strlen(password)+1);
		ret=send(SOCKET_COMUNICAZIONE_SERVER, (void*)password, strlen(password)+1, 0);
		if(ret==-1){
			perror("Errore invio password");
			exit(1);
		}
		// CONTROLLO CHE L'USERNAME NON SIA GIÀ PRESENTE
		ret=recv(SOCKET_COMUNICAZIONE_SERVER, (void*)output_server, DIM_OUTPUT_SERVER, MSG_WAITALL);
		if(ret==-1){
			perror("Errore ricezione conferma server");
			exit(1);
		}
		if(strcmp("OK", output_server)==0){
			porta_t=htons(porta);
			send(SOCKET_COMUNICAZIONE_SERVER, (void*)&ip_t, sizeof(uint32_t), 0);
			send(SOCKET_COMUNICAZIONE_SERVER, (void*)&porta_t, sizeof(uint16_t), 0);

			printf("REGISTRAZIONE AVVENUTA CON SUCCESSO!\n");
			var_uscita_while=1;
		}
		else{
			printf("REGISTRAZIONE FALLITA. Username già in uso. Reinviare i dati\n");
		}
	}
}	

void share(){
	char path[MAX_PATH];
	char share_msg[]="/share\0";
	char msg[MAX_USERNAME];
	FILE* file_to_share;
	char buffer[MAX_BUFFER_FILE];
	int dim;
	int inviati;
	int inviati_dopo;
	int i;

	inviati=0;
	inviati_dopo=0;
	strcpy(msg, USERNAME);
	strcat(msg, share_msg);
	printf("Inserisci il nome del file\n");
	scanf("%s", path);
	file_to_share=fopen(path, "r");
	if(file_to_share==NULL){
		printf("Il file che hai inserito non esiste\n");
		return;
	}
	dim=fread(buffer, sizeof(char), sizeof(buffer), file_to_share);

	// devo fare la send di share a tutti
	for(i=0; i<GRUPPO_CHAT.NUM_CLIENT_GROUP; i++){
		invio_dim_dati(GRUPPO_CHAT.sd[i], strlen(msg)+1);
		send(GRUPPO_CHAT.sd[i], (void*)msg, strlen(msg)+1, 0);
	}
	// invio il nome del file 
	for(i=0; i<GRUPPO_CHAT.NUM_CLIENT_GROUP; i++){
		invio_dim_dati(GRUPPO_CHAT.sd[i], strlen(path)+1);
		send(GRUPPO_CHAT.sd[i], (void*)path, strlen(path)+1, 0);
	}
	for(i=0; i<GRUPPO_CHAT.NUM_CLIENT_GROUP; i++){
		invio_dim_dati(GRUPPO_CHAT.sd[i], dim);
		inviati=send(GRUPPO_CHAT.sd[i],(void*)buffer, dim, 0);
		while(inviati<dim){
			inviati_dopo=send(GRUPPO_CHAT.sd[i],(void*)buffer, dim-inviati, 0);
			inviati+=inviati_dopo;
		}
	}
	printf("[FILE INVIATO]\n");
}

void gestore_chat(){
	////////////////////////// GESTORE DELLE CHAT ////////////////////////
	// la funzione si occupa di gestire i messaggi in arrivo e in uscita;
	// si occupa anche di effettuare i comandi principali: aggiungere una persona al gruppo, quittare...
	// LA FUNZIONE SI ASPETTA DI TROVARE MASTER E FDMAX PRONTO CON DENTRO GIÀ TUTTI I CLIENT
	// E GRUPPO_CHAT PRONTO

	// variabili di utilità
	int ret,i,dim_dati,len,num_client_serviti,j,k;
	char msg[MAX_MSG];
	char buffer[MAX_MSG];
	uint32_t num_t;

	// variabili nuovo client da inserire nel gruppo
	int new_client_sd;
	char username_new_client[MAX_USERNAME];
	uint32_t new_ip;
	uint16_t new_porta;
	struct sockaddr_in new_indirizzo_client;

	// variabili del client da cancellare dal gruppo
	uint32_t ip_to_del;
	uint16_t porta_to_del;

	//flag
	int exit_from_chat=0;
	int client_aggiunto=0;

	// variabili per la select
	fd_set readwrite_fds;

	// inizializzo le variabili globali del gruppo
	FD_ZERO(&readwrite_fds);

	print_cronologia();

	CHAT_ON=1;

	ret=pthread_create(&THREAD_FIGLIO_RICEZIONE, NULL, thread_receive, NULL);
	if(ret){
		printf("ERRORE NELLA CREZIONE DEL THREAD DI RECEIVE");
		exit(-1);
	}
	printf("[CHAT INIZIALIZZATA]\n");	
	printf("*******************[ISTRUZIONI]*******************\n");
	printf("/u per vedere stampare i nomi contenuti in rubrica\n");
	printf("/a per aggiungere un user alla chat\n");
	printf("/share per condividere un file con gli utenti in chat\n");
	printf("/q per uscire dalla chat\n");
	printf("**************************************************\n");
	
	while(1){
		scanf("%s", msg);
		if(strcmp("/q", msg)==0 || FLAG_USCITA_IMPROVVISA){
			printf("[ESCO DALLA CHAT]\n");
			exit_chat_client();
			// manda il mio ip e porta a tutti quelli del gruppo per farmi rimuovere
			return;
		}
		if(strcmp("/u",msg)==0){
			mostra_rubrica();
			continue;
		}
		if(strcmp("/share",msg)==0){
			share();
			continue;
		}
		if(strcmp("/a",msg)==0){
			if(GRUPPO_CHAT.NUM_CLIENT_GROUP==MAX_CLIENT_CHAT){
				printf("[IL GRUPPO È FULL]\n");
				continue;
			}
			printf("Inserisci lo username\n");
			scanf("%s", username_new_client);
			richiesta_ip_porta(SOCKET_COMUNICAZIONE_SERVER, username_new_client);
			if (RETURN_VALUE_CLIENT==1){
			printf("[USERNAME %s DISCONNESSO O GIÀ IN UN'ALTRA CHAT]\n", username_new_client);
			continue;
			}
			if(RETURN_VALUE_CLIENT==-1){
			printf("[USERNAME INESISTENTE]\n");
			continue;
			}
			if(RETURN_VALUE_CLIENT==0){
				new_client_sd=socket(AF_INET, SOCK_STREAM, 0);
				memset(&new_indirizzo_client, 0, sizeof(new_indirizzo_client));
				new_indirizzo_client.sin_family=AF_INET;
				new_indirizzo_client.sin_port=htons(PORTA_CLIENT_AGGIUNTO);
				inet_pton(AF_INET, "localhost", &new_indirizzo_client.sin_addr);
				ret=connect(new_client_sd, (struct sockaddr*)&new_indirizzo_client, sizeof(new_indirizzo_client));
				// sono connesso al nuovo client
				
				printf("[USERNAME %s SI STA PER CONNETTERE]\n",username_new_client);
				// a questo punto devo mandare al nuovo client tutti gli ip e le porte degli altri client connessi
			
				// devo mandare allo username il numero di client che fanno parte del gruppo, e per ognuno di essi ip e porta
				num_t=htonl(GRUPPO_CHAT.NUM_CLIENT_GROUP);
				send(new_client_sd, (void*)&MY_PORT_T, sizeof(uint16_t), 0);
				send(new_client_sd, (void*)&num_t, sizeof(uint32_t), 0);
				// mando tutti gli ip e porta di tutti i client connessi al gruppo, in modo che lui pottrà fare le varie connect
				for(i=0; i<GRUPPO_CHAT.NUM_CLIENT_GROUP; i++){
					// è già tutto in formato network
					send(new_client_sd, (void*)&GRUPPO_CHAT.IP_T[i], sizeof(uint32_t),0);
					send(new_client_sd, (void*)&GRUPPO_CHAT.PORT_T[i], sizeof(uint16_t),0);
				}
				// arrivato a questo punto il client appena connesso conosce tutti i dati per connettersi a tutti gli altri client
				// DEVO AGGIUNGERE IL NUOVO CLIENT AL MASTER, ma non subito altrimenti manderei anche a lui il messaggio di aggiungere qualcuno. Setto un flag e lo sistemo dopo
				client_aggiunto=1;
			}
		}
		// se è stato mandato un messaggio normale, o un messaggio di quit, o un messaggio per aggiungere un nuovo client, devo mandarlo agli altri
		strcpy(buffer, USERNAME);
		strcat(buffer, msg);
		readwrite_fds=MASTER;
		num_client_serviti=0;

		// in buffer c'è il messaggio da inviare
		save_msg_cronologia(DUE_SPUNTE);
		save_msg_cronologia(buffer);

		// invio del messaggio
		while(num_client_serviti!=GRUPPO_CHAT.NUM_CLIENT_GROUP){
			select(FDMAX+1, NULL, &readwrite_fds, NULL, NULL);
			for(i=0; i<=FDMAX; i++){
				if(FD_ISSET(i, &readwrite_fds)){
					invio_dim_dati(i, strlen(buffer)+1);
					send(i,(void*)buffer, strlen(buffer)+1, 0);
					num_client_serviti++;
					FD_CLR(i, &readwrite_fds);  // ogni volta tolgo dal set il client a cui ho spedito.
				}
			}	
		}
		if(client_aggiunto){
			FD_SET(new_client_sd, &MASTER);
			if(new_client_sd>FDMAX){
				FDMAX=new_client_sd;
			}
			// aggiungo il nuovo client alla struttura del gruppo
			GRUPPO_CHAT.IP_T[GRUPPO_CHAT.NUM_CLIENT_GROUP]=htonl(new_ip);
			GRUPPO_CHAT.PORT_T[GRUPPO_CHAT.NUM_CLIENT_GROUP]=htonl(new_porta);
			GRUPPO_CHAT.sd[GRUPPO_CHAT.NUM_CLIENT_GROUP]=new_client_sd;
			GRUPPO_CHAT.NUM_CLIENT_GROUP++;
			client_aggiunto=0;
		}
	}
}

void chat_client_connesso(uint32_t ip, uint16_t porta){
	
	// variabili di utilità
	int client_sd;
	struct sockaddr_in indirizzo_client;
	int ret,i;
	uint32_t NUM_UTENTI=0;

	GRUPPO_CHAT.NUM_CLIENT_GROUP=0;

	// creo il socket con il client
	client_sd=socket(AF_INET, SOCK_STREAM, 0);
	memset(&indirizzo_client, 0, sizeof(indirizzo_client));
	indirizzo_client.sin_family=AF_INET;
	indirizzo_client.sin_port=htons(porta);
	inet_pton(AF_INET, "localhost", &indirizzo_client);
	ret=connect(client_sd, (struct sockaddr*)&indirizzo_client, sizeof(indirizzo_client));
	if(ret==-1){
		perror("errore in fase di connessione al client");
		exit(1);
	}
	//////// CONNESSIONE CON I 2 CLIENT APERTA ///////
	NUM_UTENTI=htonl(NUM_UTENTI);
	send(client_sd, (void*)&MY_PORT_T, sizeof(uint16_t), 0);
	send(client_sd, (void*)&NUM_UTENTI, sizeof(uint32_t), 0);

	GRUPPO_CHAT.IP_T[0]=htonl(ip);
	GRUPPO_CHAT.PORT_T[0]=htons(porta);  // è la porta di listen
	GRUPPO_CHAT.NUM_CLIENT_GROUP=1;
	GRUPPO_CHAT.sd[0]=client_sd;


	FD_ZERO(&MASTER);

	FD_SET(client_sd, &MASTER);
	FDMAX=client_sd;

	gestore_chat();
}

void print_hanging(){
	uint16_t dim;
	uint16_t num_chat_pendenti;
	uint16_t num_msg;
	char username [MAX_USERNAME];
	char timestamp[TIME_LENGTH+1];
	int i;

	printf("[MESSAGGI PENDENTI]\n");
	// mi faccio inviare il numero di chat pendenti
	recv(SOCKET_COMUNICAZIONE_SERVER, (void*)&num_chat_pendenti, sizeof(uint16_t), MSG_WAITALL);
	num_chat_pendenti=ntohs(num_chat_pendenti);
	for(i=0; i<num_chat_pendenti; i++){
		// username -> timestamp -> num_msg
		recv(SOCKET_COMUNICAZIONE_SERVER, (void*)&dim, sizeof(uint16_t), MSG_WAITALL);
		dim=ntohs(dim);
		recv(SOCKET_COMUNICAZIONE_SERVER, (void*)username, dim, MSG_WAITALL);
		recv(SOCKET_COMUNICAZIONE_SERVER, (void*)timestamp, TIME_LENGTH, MSG_WAITALL);
		timestamp[TIME_LENGTH]='\0';
		recv(SOCKET_COMUNICAZIONE_SERVER, (void*)&num_msg, sizeof(uint16_t), MSG_WAITALL);
		num_msg=ntohs(num_msg);

		// a questo punto printo tutto
		printf("[%s]: Numero messaggi pendenti: %d Timestamp ultimo messaggio: %s\n", username, num_msg, timestamp);
	}
}

void print_hanging_msg(){
	// ricevo in ritorno l'username. Se ho sbagliato lo username mi arriverà /NULL in username
	char username[MAX_USERNAME];
	uint16_t len_username;
	uint16_t dim_buffer;
	char buffer[MAX_CRONOLOGIA];
	char msg[MAX_MSG];
	int i,k;
	int flag_first_msg=1;

	recv(SOCKET_COMUNICAZIONE_SERVER, (void*)&len_username, sizeof(uint16_t), MSG_WAITALL);
	len_username=ntohs(len_username);
	recv(SOCKET_COMUNICAZIONE_SERVER, (void*)username, len_username, MSG_WAITALL);
	if(strcmp(username, "/NULL")==0){
		// significa che non ho messaggi pendenti da quell'user
		printf("[NON HAI MESSAGGI PENDENTI DALL'USERNAME SELEZIONATO]\n");
		return;
	}
	printf("[%s TI HA INVIATO I SEGUENTI MESSAGGI]\n", username);
	recv(SOCKET_COMUNICAZIONE_SERVER, (void*)&dim_buffer, sizeof(uint16_t), MSG_WAITALL);
	dim_buffer=ntohs(dim_buffer);
	recv(SOCKET_COMUNICAZIONE_SERVER, (void*)buffer, dim_buffer, MSG_WAITALL);
	// a questo punto in buffer ho i messaggi
	for(i=0; buffer[i]!='\0'; i++){
		if(buffer[i]=='|'){
			if(flag_first_msg){
				flag_first_msg=0;
				k=0;
				continue;
			}
			msg[k]='\0';
			printf("%s\n", msg);
			k=0;
			continue;
		}
		msg[k]=buffer[i];
		k++;
	}
	// è l'ultimo messaggio
	msg[k]='\0';
	printf("%s\n", msg);
}

void* comunicazioni_server(void* arg){
	// variabili di utilità
	char msg[MAX_MSG];
	int len;
	uint16_t len_username;
	char username[MAX_USERNAME];
	char username_contatto[MAX_USERNAME];
	int ret;
	char file_path[MAX_USERNAME];

	// COSA POSSO RICEVERE? O UNA RICHIESTA DA UN CLIENT, O UNA RICHIESTA DAL SERVER
	// RICHIESTA SERVER [ACKRQ]-> O è una richiesta per un ACK
	// RICHIESTA SERVER [CHREQ] -> è una richiesta per chiedermi se voglio entrare a far parte della chat
	//   =>
	// RICHIESTA CLIENT -> l'altro client fa la connect e poi mi manda tutte le ip e porte
	while(1){
		ret=recv(SOCKET_COMUNICAZIONE_SERVER, (void*)msg, DIM_INPUT_SERVER, MSG_WAITALL);
		if(ret==-1){
			perror("ERRORE RECV THREAD SERVER: ");
			exit(1);
		}
		if(ret==0){
			printf("[SERVER DOWN]\n");
			FLAG_SERVER_DOWN=1;
			pthread_exit(NULL);
		}
		if(strcmp(USRCO, msg)==0){
			printf("[LO USER HA ACCETTATO]\n");
			recv(SOCKET_COMUNICAZIONE_SERVER, (void*)&IP_CLIENT_AGGIUNTO, sizeof(uint32_t), MSG_WAITALL);
			recv(SOCKET_COMUNICAZIONE_SERVER, (void*)&PORTA_CLIENT_AGGIUNTO, sizeof(uint16_t), MSG_WAITALL);
			IP_CLIENT_AGGIUNTO=ntohl(IP_CLIENT_AGGIUNTO);
			PORTA_CLIENT_AGGIUNTO=ntohs(PORTA_CLIENT_AGGIUNTO);
			RETURN_VALUE_CLIENT=0;
			pthread_cond_signal(&RISPOSTA_ARRIVATA);
			continue;
		}
		if(strcmp(USRDS, msg)==0){
			RETURN_VALUE_CLIENT=1;
			pthread_cond_signal(&RISPOSTA_ARRIVATA);
			continue;
		}
		if(strcmp(USRNE, msg)==0){
			printf("[USER NON ESISTENTE]\n");
			RETURN_VALUE_CLIENT=-1;
			pthread_cond_signal(&RISPOSTA_ARRIVATA);
			continue;
		}
		if(strcmp(CHREQ, msg)==0){
			fflush(stdout);
			// se sono già in chat con qualcuno non posso rispondere
			if(CHAT_ON==1 || CHAT_SERVER==1){
				printf("[HAI UNA NUOVA NOTIFICA]\n");
				continue;
			}
			// qualcuno mi sta chiedendo di parlare con lui
			// devo farmi passare lo username di chi sta provando a contattarmi
			len=ricezione_dim_dati(SOCKET_COMUNICAZIONE_SERVER);
			recv(SOCKET_COMUNICAZIONE_SERVER, (void*)username_contatto, len, MSG_WAITALL);
			printf("[LO USER %s HA PROVATO A CONTATTARTI]\n", username_contatto);
			if(CHAT_SERVER==0 && CHAT_ON==0){
				printf("[PREMI 6 PER GESTIRE LA NOTIFICA]\n");
				// preparo il PATH
				strcpy(file_path, MYSELF);
				strcat(file_path, username_contatto);
				sprintf(PATH, "cronologia/%s.txt", file_path);
				continue;
			}
		}
		if(strcmp(CHHNG, msg)==0){
			print_hanging();
		}
		if(strcmp(SHHNG, msg)==0){
			print_hanging_msg();
		}
		if(strcmp(NOTIF, msg)==0){
			recv(SOCKET_COMUNICAZIONE_SERVER, (void*)&len_username, sizeof(uint16_t), MSG_WAITALL);
			len_username=ntohs(len_username);
			recv(SOCKET_COMUNICAZIONE_SERVER, (void*)username, len_username, MSG_WAITALL);
			printf("[L'UTENTE %s HA APPENA VISUALIZZATO I TUOI MESSAGGI PENDENTI]\n", username);
		}
		if(strcmp(SERDO, msg)==0){
			printf("[SERVER DOWN]\n");
			pthread_exit(NULL);
		}
	}
}

void gestione_notifica(){
	// significa che ero nella home e il server mi ha notificato che qualcuno vuole aggiungermi a una chat.
	// variabili di utilità
	char msg[MAX_MSG];
	uint32_t num_utenti_gruppo;
	uint16_t porta_utente;
	uint16_t porta_ascolto_utente;
	uint32_t ip_utente;
	struct sockaddr_in indirizzo;
	int len,i, sd_new_client;
	int sd_client;
	int ret;

	FD_ZERO(&MASTER);
	printf("Vuoi partecipare alla chat? /si o /no\n");
	scanf("%s", msg);
	if(strcmp(msg, "/si")==0){
		printf("Stai per essere aggiunto alla chat..\n");
		// notifico al server che intendo connettermi, in modo che lui mandi ip e porta all'altro client
		send(SOCKET_COMUNICAZIONE_SERVER, (void*)CONAC, DIM_INPUT_SERVER, 0);
		len=sizeof(indirizzo);
		sd_client=accept(SOCKET_ASCOLTO, (struct sockaddr*)&indirizzo, &len);
		if(sd_client==-1){
			perror("ERRORE ACCEPT: ");
		}
		recv(sd_client, (void*)&porta_ascolto_utente, sizeof(uint16_t), MSG_WAITALL);
		// aggiorno il gruppo
		GRUPPO_CHAT.NUM_CLIENT_GROUP=1;
		GRUPPO_CHAT.IP_T[0]=indirizzo.sin_addr.s_addr;
		GRUPPO_CHAT.PORT_T[0]=porta_ascolto_utente;
		GRUPPO_CHAT.sd[0]=sd_client;
		FD_SET(sd_client, &MASTER);
		FDMAX=sd_client;
		
		// ricevo il numero di persone appartenenti al gruppo
		recv(sd_client, (void*)&num_utenti_gruppo, sizeof(uint32_t), MSG_WAITALL);
		num_utenti_gruppo=ntohl(num_utenti_gruppo);
		printf("[num utenti gruppo: %d]\n", num_utenti_gruppo+1);
		for(i=0; i<num_utenti_gruppo; i++){
			recv(sd_client, (void*)&ip_utente, sizeof(uint32_t), MSG_WAITALL);
			recv(sd_client, (void*)&porta_utente, sizeof(uint16_t), MSG_WAITALL);

			// mi connetto al socket di quell'utente;
			sd_new_client=socket(AF_INET, SOCK_STREAM, 0);
			memset(&indirizzo, 0, sizeof(indirizzo));
			indirizzo.sin_family=AF_INET;
			indirizzo.sin_port=porta_utente;
			indirizzo.sin_addr.s_addr=ip_utente;
			ret=connect(sd_new_client, (struct sockaddr*)&indirizzo, sizeof(indirizzo));
			if(ret==-1){
				perror("[ERRORE NELLA CONNECT GESTIONE NOTIFICA: ");
				exit(1);
			}
			// aggiorno il gruppo
			GRUPPO_CHAT.IP_T[GRUPPO_CHAT.NUM_CLIENT_GROUP]=ip_utente;
			GRUPPO_CHAT.PORT_T[GRUPPO_CHAT.NUM_CLIENT_GROUP]=porta_utente;
			GRUPPO_CHAT.sd[GRUPPO_CHAT.NUM_CLIENT_GROUP]=sd_new_client;
			GRUPPO_CHAT.NUM_CLIENT_GROUP++;

			FD_SET(sd_new_client, &MASTER);
			if(FDMAX<sd_new_client){
				FDMAX=sd_new_client;
			}
			// mando la porta del mio socket di ascolto
			send(sd_new_client, (void*)&MY_PORT_T, sizeof(uint16_t), 0);
		}
		// arrivato qua ho nel master tutti i client del gruppo, fdmax è sistemato e tutti i client sono registrati in GRUPPO_CHAT;
		gestore_chat();
		return;
	}
	else {
		printf("Non verrai aggiunto alla chat");
		send(SOCKET_COMUNICAZIONE_SERVER, (void*)CONRE, DIM_INPUT_SERVER, 0);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////// FUNZIONI APPLICATIVO ////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
void hanging(){
	// richiedo le chat hanging
	send(SOCKET_COMUNICAZIONE_SERVER, (void*)CHHNG, DIM_INPUT_SERVER, 0);
}

void show(){
	// richiedo i messaggi in hanging
	char username[MAX_USERNAME];
	uint16_t len_username;
	printf("INSERISCI UN USERNAME\n");
	scanf("%s", username);
	len_username=htons(strlen(username)+1);

	send(SOCKET_COMUNICAZIONE_SERVER, (void*)SHHNG, DIM_INPUT_SERVER, 0);
	send(SOCKET_COMUNICAZIONE_SERVER, (void*)&len_username, sizeof(uint16_t), 0);
	send(SOCKET_COMUNICAZIONE_SERVER, (void*)username, strlen(username)+1, 0);
}

void chat(){
	char username[MAX_USERNAME];
	char file_path[MAX_USERNAME];
	int ret;
	printf("Inserisci username destinatario:\n");
	scanf("%s", username);
	
	// sistemo il path
	strcpy(file_path, MYSELF);
	strcat(file_path, username);
	sprintf(PATH, "cronologia/%s.txt", file_path);
	
	// devo aprire un socket di comunicazione con l'altro client, e devo quindi farmi dare ip e porta
	richiesta_ip_porta(SOCKET_COMUNICAZIONE_SERVER, username);
	if(RETURN_VALUE_CLIENT==-1){
		return;
	}
	if(RETURN_VALUE_CLIENT==0){
		CHAT_ON=1;
		chat_client_connesso(IP_CLIENT_AGGIUNTO, PORTA_CLIENT_AGGIUNTO);
		return;
	}
	if(RETURN_VALUE_CLIENT==1){
		CHAT_SERVER=1;
		chat_client_disconnesso(username);  // faccio soltanto send e nessuna receive
		return;
	}
}

void out(){
	exit(1);
}

void show_menu(){
	while(1){
		// inizializzo le variabili
		FD_ZERO(&MASTER);
		GRUPPO_CHAT.NUM_CLIENT_GROUP=0;
		FDMAX=0;
		int scelta_utente;
		CHAT_SERVER=0;
		CHAT_ON=0;
		int c;

		if(FLAG_SERVER_DOWN){
			printf("[IL SERVER È DISCONNESSO. USCITA FORZATA]\n");
			exit(1);
		}

		// menù iniziale:
		printf("BENVENUTO NEL MENU: SELEZIONARE IL NUMERO DEL COMANDO\n");
		printf("1) Hanging -> Permette di ricevere la lista degli utenti che hanno inviato messaggi mentre eri offline\n");
		printf("2) Show <username> -> Permette di ricevere i messaggi pendenti dall'utente username\n");
		printf("3) Chat <username> -> Avvia una chat con l'utente username\n");
		printf("4) Out -> Uscita dall'applicazione\n");

		scanf("%d", &scelta_utente);
		
		if(FLAG_SERVER_DOWN){
			printf("[IL SERVER È DISCONNESSO. USCITA FORZATA]\n");
			exit(1);
		}

		switch(scelta_utente){
			case 1:
				printf("Hanging..\n");
				hanging();
				continue;
			case 2:
				printf("Show..\n");
				show();
				continue;
			case 3:
				printf("Chat..\n");
				chat();
				continue;
			case 4:
				printf("Out..\n");
				out();
				continue;
			case 6:
				gestione_notifica();
				continue;
			default:
				printf("Comando errato: %d\n", scelta_utente);
		}
	}
}

/////////////////// MAIN ///////////////////////////
int main(int argc, char**argv){
	// variabili di connessione al server
	int ret, sd,i,porta;
	struct sockaddr_in server_addr, my_addr, my_ack_addr;

	// variabili del client
	uint16_t porta_t;
	struct in_addr ip;
	uint32_t ip_t;

	char username [MAX_USERNAME];

	// username di chi mi contatta
	char username_contatto [MAX_USERNAME];

	// variabili di utility
	int scelta_utente;
	int len;
	int var_uscita_while=0;
	GRUPPO_CHAT.NUM_CLIENT_GROUP=0;
	pid_t processo_listener;
	char msg[MAX_MSG];
	int optval = 1;

	if(argc<=1){
		printf("[NON HAI INSERITO UNA PORTA]\n");
		exit(1);
	}
	porta=atoi(argv[1]);

	// signal per permettere al processo in ascolto di bloccare il padre
	signal(SIGINT, handler_close);
	PROCESSO_PADRE=getpid();
	
	// creazione del socket dell'utente DI ASCOLTO
	SOCKET_ASCOLTO=socket(AF_INET, SOCK_STREAM, 0);
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family=AF_INET;
	my_addr.sin_port=htons(porta);
	inet_pton(AF_INET, "localhost", &my_addr.sin_addr);

	setsockopt(SOCKET_ASCOLTO,SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	ret=bind(SOCKET_ASCOLTO, (struct sockaddr*)&my_addr, sizeof(my_addr));
	if(ret==-1){
		perror("Error bind client: ");
		exit(1);
	}
	
	// connessione al server
	SOCKET_COMUNICAZIONE_SERVER=socket(AF_INET, SOCK_STREAM, 0);
	if(SOCKET_COMUNICAZIONE_SERVER==-1){
		perror("Errore nella creazione del socket");
		exit(1);
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family=AF_INET;
	server_addr.sin_port=htons(SRV_PORT);
	inet_pton(AF_INET, "localhost", &server_addr.sin_addr);
	
	ret=connect(SOCKET_COMUNICAZIONE_SERVER, (struct sockaddr*)&server_addr, sizeof(server_addr));

	if(ret==-1){
		perror("Errore in fase di connessione al server");
		exit(1);
	}
	
	ret=listen(SOCKET_ASCOLTO, MAX_CLIENT_CHAT);
	if(ret==-1){
		perror("ERRORE SULLA LISTEN");
	}

	// inizializzo le variabili di sincronizzazione
	pthread_mutex_init(&MUTEX, NULL);
	pthread_cond_init(&RISPOSTA_ARRIVATA, NULL);

	// se arrivo a questo punto sono connesso al server
	while(1){
		printf("Inserisci il numero corrispondente all'operazione\n");
		printf("1) SIGNUP\n");
		printf("2) IN\n");
		scanf("%d", &scelta_utente);
		switch(scelta_utente){
			case 1: 
				sigin(porta, username);
				var_uscita_while=1;
				break;
			case 2:  // LOGIN
				login(username);
				var_uscita_while=1;
				break;
			default:
				printf("Numero inserito non corretto. Reinserire numero \n");
				break;
		}
		if(var_uscita_while){  // esco dal ciclo soltanto se ho effettuato login o signup correttamente, altrimenti continuo a ciclare
			break;
		}
	}
	var_uscita_while=0;
	// arrivato qua sono terminate tutte le procedure di sign in/ log in, e sono connesso col server
	CHAT_ON=0;
	CHAT_SERVER=0;
	FLAG_SERVER_DOWN=0;
	// mi serve per printare bene lo username che sta scrivendo dalla funzione chat()
	strcpy(USERNAME, username);
	strcpy(MYSELF, username);
	len=strlen(USERNAME);
	USERNAME[len]=':';
	USERNAME[len+1]=' ';
	USERNAME[len+2]='\0';

	// Se l'accesso è avvenuto con successo, registro il mio IP e la mia PORTA
	MY_PORT_T=htons(porta);
	inet_pton(AF_INET, "localhost", &ip);
	MY_IP_T=htonl(ip.s_addr);
	

	pthread_mutex_init(&MUTEX_FILE, NULL);
	// THREAD CHE SI OCCUPA DELLE COMUNICAZIONI COL SERVER
	ret=pthread_create(&THREAD_COMUNICAZIONI_SERVER, NULL, comunicazioni_server, NULL);

	// PROCESSO PADRE
	show_menu();
}
