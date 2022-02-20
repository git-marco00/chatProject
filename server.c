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
#include <time.h>
#include <stdlib.h>
#include <pthread.h>


#define MAX_USERS 20
#define MAX_USERNAME 1024
#define MAX_PASSWORD 1024
#define DIM_OUTPUT_SERVER 3  // OK\0  NO\0 
#define DIM_MSG_SERVER 7 // /LOGIN\0 /SIGIN\0
#define SRV_PORT 4242
#define MAX_CLIENT_CHAT 10
#define TIME_LENGTH 19   //yyyy:mm:gg:hh:mm:ss
#define MAX_DIM_GRUPPO 20
#define MAX_GRUPPI 10
#define MAX_MSG 1024
#define MAX_CRONOLOGIA 4096


// comunicazioni col server
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

char OK[]="OK\0";
char NO[]="NO\0";

int SERVER_PORT=4242;

struct PENDENTI{
    char username[MAX_USERNAME];
    char timestamp[TIME_LENGTH];
    char buffer[MAX_CRONOLOGIA];
    int num_msg;
    struct PENDENTI* next;
};

struct NOTIFICA{
    char username[MAX_USERNAME];
    struct NOTIFICA* next;
};

struct user{
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    uint16_t porta;
    char timestamp_login [TIME_LENGTH];
    char timestamp_logout [TIME_LENGTH];
    int sd;
    int flag_chat; 
    struct PENDENTI* chat_pendenti;
    struct NOTIFICA* notifiche_pendenti;
};
struct user USERS[MAX_USERS];



// variabili dei thread
pthread_t REC_THREAD;
pthread_t ACK_THREAD;

// variabili globali su cui andranno ad agire i thread
fd_set MASTER;
int FDMAX;

int LISTENER;

////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// FUNZIONI DI UTILITÀ ////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
void mostra_comandi(){
    printf("MOSTRO I COMANDI\n");
}

void mostra_utenti_online(){
    int i;
    for(i=0; i<MAX_USERS; i++){
        if(strcmp(USERS[i].timestamp_logout, "\0")==0){
            printf("%s*%s*%d\n", USERS[i].username, USERS[i].timestamp_login, ntohs(USERS[i].porta));
        }
    }
}

void close_server(){
    // faccio una send a tutti i client online
    int i;
    for(i=0; i<MAX_USERS; i++){
        if(strcmp(USERS[i].timestamp_logout, "\0")==0){
            send(USERS[i].sd, (void*)SERDO, DIM_MSG_SERVER, 0);
            // devo chiudere tutti i socket aperti
            close(USERS[i].sd);
        }
    } 
}

void iniz_variabili(){
    int i,j;
    for(i=0; i<MAX_USERS; i++){
        strcpy(USERS[i].username, "\0");
        USERS[i].flag_chat=0;
        strcpy(USERS[i].timestamp_logout, "NULL");
        USERS[i].chat_pendenti=NULL;
        USERS[i].notifiche_pendenti=NULL;
    }
    FD_ZERO(&MASTER);
    FDMAX=0;
}

void modifica_login_logout(int loginOut, char*login, char*logout){
    // loginOut==0 -> modifico login
    // loginOut==1 -> modifico logout
    // var di utilità
    time_t rawtime;
    struct tm* timeinfo;

    time(&rawtime);
    timeinfo=localtime(&rawtime);

    if(loginOut==0){
        sprintf(login, "%d:%d:%d:%d:%d", timeinfo->tm_year+1900, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        sprintf(logout, "\0");
    }
    else{
        sprintf(logout, "%d:%d:%d:%d:%d", timeinfo->tm_year+1900, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    }
}

void invio_notifica_pendenti(int sd, char* username){
    // in sd ho il socket dello user che aveva mandato i messaggi, in username ho il nome di quello che era offline
    uint16_t len_username;
    
    send(sd, (void*)NOTIF, DIM_MSG_SERVER, 0);
    len_username=htons(strlen(username)+1);
    send(sd, (void*)&len_username, sizeof(uint16_t), 0);
    send(sd, (void*)username, strlen(username)+1, 0);
}

void show_menu(){
    int input;
    while(1){
        printf("[SERVER STARTED]\n");
        printf("Digita un comando\n");
        printf("1) help --> mostra i dettagli dei comandi\n");
        printf("2) list --> mostra un elenco degli utenti connessi\n");
        printf("3) esc --> chiude il server\n");
        scanf("%d", &input);
        if(input<1 || input >3){
            printf("Comando sconosciuto\n");
            continue;
        }
        switch(input){
            case 1:
                mostra_comandi();
                break;
            case 2:
                mostra_utenti_online();
                break;
            case 3:
                close_server();
                return;
        }
    }
}

void richiesta_login(int sd){
    // var di utilità
    uint16_t dim;
    char username [MAX_USERNAME];
    char password [MAX_PASSWORD];
    int i,ret;
    struct sockaddr_in address;
    int sd_ack_addr;
    struct NOTIFICA* list;

    recv(sd, (void*)&dim, sizeof(uint16_t), MSG_WAITALL);
    dim=ntohs(dim);
    recv(sd, (void*)username, dim, MSG_WAITALL);
    recv(sd, (void*)&dim, sizeof(uint16_t), MSG_WAITALL);
    dim=ntohs(dim);
    recv(sd, (void*)password, dim, MSG_WAITALL);

    // scorro per trovare un username e una password uguali a quelli arrivati
    for(i=0; i<MAX_USERS; i++){
        if(strcmp(USERS[i].username, username)==0 && strcmp(USERS[i].password, password)==0){
            // user trovato
            ret=send(sd, (void*)OK, 3, 0);
            if(ret==-1){
                perror("ERRORE NELLA SEND DI LOGIN: ");
            }
            // aggiorno le mie entry
            modifica_login_logout(0, USERS[i].timestamp_login, USERS[i].timestamp_logout);
            USERS[i].sd=sd;

            // controllo se l'utente abbia dei messaggi pendenti
            if(USERS[i].chat_pendenti!=NULL){
                send(sd, (void*)NOTSI, DIM_MSG_SERVER, 0);
            }
            else send(sd, (void*)NOTNO, DIM_MSG_SERVER, 0);

            // controllo se ci siano notifiche di visualizzazione pendenti per l'utente e in caso gliele invio
            for(list=USERS[i].notifiche_pendenti; list!=NULL; list=list->next){
                invio_notifica_pendenti(USERS[i].sd, list->username);
            }
            USERS[i].notifiche_pendenti=NULL;
            return;
        }
    }
    send(sd, (void*)NO, 3, 0);
}

void richiesta_sigin(int sd){
    // var di utilità
    uint16_t dim;
    uint32_t ip;
    uint16_t porta;
    char username [MAX_USERNAME];
    char password [MAX_PASSWORD];
    int i,j,ret;

    for(i=0; i<MAX_USERS; i++){
        if(strcmp(USERS[i].username, "\0")==0){
            // ho trovato un posto buono in cui inserire l'utente
            recv(sd, (void*)&dim, sizeof(uint16_t), MSG_WAITALL);
            dim=ntohs(dim);
            recv(sd, (void*)username, dim, MSG_WAITALL);
            recv(sd, (void*)&dim, sizeof(uint16_t), MSG_WAITALL);
            dim=ntohs(dim);
            recv(sd, (void*)password, dim, MSG_WAITALL);
            // controllo che lo username non sia gia in uso da qualcuno
            for(j=0; j<MAX_USERS; j++){
                if(strcmp(USERS[j].username, username)==0){
                    send(sd, (void*)NO, 3, 0);
                    return;
                }
            }
            send(sd, (void*)OK, 3, 0);
            recv(sd, (void*)&ip, sizeof(uint32_t), MSG_WAITALL);
            recv(sd, (void*)&porta, sizeof(uint16_t), MSG_WAITALL);
            // aggiungo tutto nelle entry
            strcpy(USERS[i].username, username);
            strcpy(USERS[i].password, password);
            USERS[i].sd=sd;
            USERS[i].porta=porta;  // la salvo in formato network
            modifica_login_logout(0, USERS[i].timestamp_login, USERS[i].timestamp_logout);
            return;
        }
    }
}

void richiesta_ip_porta(int sd, char*username_request){
    uint16_t dim;
    char username_to_contact[MAX_USERNAME];
    int i,ret,j;
    char buffer[DIM_MSG_SERVER];
    uint32_t ip_client_to_contact;
    uint16_t porta_client_to_contact;
    struct in_addr addr_client_to_contact;

    recv(sd, (void*)&dim, sizeof(uint16_t), MSG_WAITALL);
    dim=ntohs(dim);
    recv(sd,(void*)username_to_contact, dim, MSG_WAITALL);
    printf("[USERNAME %s HA RICHIESTO IP E PORTA DELL'USERNAME %s]\n", username_request, username_to_contact);
    for(i=0; i<MAX_USERS; i++){
        if(strcmp(USERS[i].username, username_to_contact)==0){
            printf("[CORRISPONDENZA TROVATA]\n");
            if(USERS[i].flag_chat){
                // se lo username_to_contact si trova già in chat con qualcuno mando USRDS
                send(sd, (void*)USRDS, DIM_MSG_SERVER, 0);
                // avverto lo username_to_contact che qualcuno ha provato a contattarlo
                send(USERS[i].sd, (void*)CHREQ, DIM_MSG_SERVER, 0);
                return;
            }
            // ho trovato l'username corrispondente, devo contattarlo
            send(USERS[i].sd, (void*)CHREQ, DIM_MSG_SERVER, 0);
            dim=strlen(username_request)+1;
            dim=htons(dim);
            send(USERS[i].sd, (void*)&dim, sizeof(uint16_t), 0);
            send(USERS[i].sd, (void*)username_request,strlen(username_request)+1 , 0);
            recv(USERS[i].sd, (void*)buffer, DIM_MSG_SERVER, MSG_WAITALL);
            if(strcmp(buffer, CONRE)==0 || strcmp(USERS[i].timestamp_logout,"\0")!=0){
                // mando un segnale di user disconnesso sia che l'altro client rifiuti la connessione
                // sia che sia offline
                send(sd, (void*)USRDS, DIM_MSG_SERVER, 0);
                return;
            }
            if(strcmp(buffer, CONAC)==0){
                ret=send(sd, (void*)USRCO, DIM_MSG_SERVER, 0);
                if(ret==-1 || ret==0){
                    perror("ERRORE SEND: ");
                }
                // devo mandare ip e porta
                printf("[MANDO IP E PORTA AL CLIENT CHE HA FATTO RICHIESTA DEL CLIENT CONTATTATO]\n");
                inet_pton(AF_INET, "localhost", &addr_client_to_contact);
                ip_client_to_contact=htonl(addr_client_to_contact.s_addr);
                send(sd, (void*)&ip_client_to_contact, sizeof(uint32_t), 0);
                porta_client_to_contact=USERS[i].porta;
                send(sd, (void*)&porta_client_to_contact, sizeof(uint16_t), 0);

                // devo aggiornare la struttura dati dei 2 user mettendo il flag chat a 1
                USERS[i].flag_chat=1;
                for(j=0; j<MAX_USERS; j++){
                    if(strcmp(USERS[j].username, username_request)==0){
                        USERS[j].flag_chat=1;
                    }
                }
                return;
            }
        }
    }
    // non ho trovato l'username
    send(sd, (void*)USRNE, DIM_MSG_SERVER, 0);
}

void scrivi_in_lista(struct user* user, char* msg){
    char username[MAX_USERNAME];
    char msg_modified[MAX_MSG];
    int i=0;

    struct PENDENTI* next_elem;
    struct PENDENTI* prec_elem;

    // variabili per il calcolo del timestamp
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo=localtime(&rawtime);

    for(i=0; msg[i]!=':'; i++){
        username[i]=msg[i];
    }
    username[i]='\0';

    sprintf(msg_modified, "|%s", msg);

    // caso in cui inserisco il primo elemento
    if(user->chat_pendenti==NULL){
        user->chat_pendenti=malloc(sizeof(struct PENDENTI));
        strcpy(user->chat_pendenti->buffer, msg_modified);
        user->chat_pendenti->num_msg=1;
        strcpy(user->chat_pendenti->username, username);
        // trovo il timestamp attuale
        sprintf(user->chat_pendenti->timestamp, "%d:%d:%d:%d:%d", timeinfo->tm_year+1900, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        user->chat_pendenti->next=NULL;
        return;
    }
    else {
        for(next_elem=user->chat_pendenti; next_elem!=NULL; next_elem=next_elem->next){
            if(strcmp(next_elem->username, username)==0){
                // ci sono già messaggi pendenti da quell'utente
                strcat(next_elem->buffer, msg_modified);
                next_elem->num_msg++;
                sprintf(next_elem->timestamp, "%d:%d:%d:%d:%d", timeinfo->tm_year+1900, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
                return;
            }
            prec_elem=next_elem;
        }
        // se arrivo qua significa che non ho trovato nessuno che mi abbia già mandato un messaggio, devo fare un nuovo inserimento
        prec_elem->next=malloc(sizeof(struct PENDENTI));
        strcpy(prec_elem->next->buffer, msg_modified);
        prec_elem->next->num_msg=1;
        strcpy(prec_elem->next->username, username);
        // trovo il timestamp attuale
        sprintf(prec_elem->next->timestamp, "%d:%d:%d:%d:%d", timeinfo->tm_year+1900, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        prec_elem->next->next=NULL;
        return;
    }
}

void store_msg(int sd){
    char msg_completo[MAX_MSG];
    char msg[MAX_MSG];
    char path[MAX_MSG];
    uint16_t dim;
    char user_to_contact[MAX_USERNAME];
    int i, j=0;


    recv(sd, (void*)&dim, sizeof(uint16_t), MSG_WAITALL);
    dim=ntohs(dim);
    recv(sd, (void*)msg_completo, dim, MSG_WAITALL);
    // a questo punto dentro msg_completo ho scritto il messaggio
    // user_to_contact.msg
    for(i=0; msg_completo[i]!='.'; i++){
        user_to_contact[i]=msg_completo[i];
    }
    user_to_contact[i]='\0';
    i++;
    for(; msg_completo[i]!='\0'; i++){
        msg[j]=msg_completo[i];
        j++;
    }
    msg[j]='\0';
    j++;
    for(i=0; i<MAX_USERS; i++){
        if(strcmp(USERS[i].username, user_to_contact)==0){
            // scrivo nella struttura dati
            scrivi_in_lista(&USERS[i], msg);
            break;
        }
    }
}

void print_test(int sd){
    int i;
    struct PENDENTI* list;

    for(i=0; i<MAX_USERS; i++){
        if(USERS[i].chat_pendenti!=NULL){
            printf("TEST PER VEDERE SE MEMORIZZO CORRETTAMETNE\n");
            for(list=USERS[i].chat_pendenti; list!=NULL; list=list->next){
                printf("%s\n", list->username);
                printf("%s\n", list->timestamp);
                printf("%d\n", list->num_msg);
                printf("%s\n", list->buffer);
            }
        }
    }
}

void send_hanging(int sd){
    int i;
    uint16_t count=0;
    uint16_t len_username;
    uint16_t num_msg;
    struct PENDENTI* list;

    for(i=0; i<MAX_USERS; i++){
        if(USERS[i].sd==sd){
            printf("[USER %s HA RICHIESTO HANGING]\n", USERS[i].username);

            // protocollo di hanging
            send(sd, (void*)CHHNG, DIM_MSG_SERVER, 0);

            // mando il numero di chat pendenti
            for(list=USERS[i].chat_pendenti; list!=NULL; list=list->next){
                count++;
            }
            count=htons(count);
            send(sd, (void*)&count, sizeof(uint16_t), 0);
            
            // mando tutte le chat pendenti
            for(list=USERS[i].chat_pendenti; list!=NULL; list=list->next){
                len_username=strlen(list->username)+1;
                len_username=htons(len_username);
                send(sd, (void*)&len_username, sizeof(uint16_t), 0);
                send(sd, (void*)list->username, strlen(list->username)+1, 0);
                send(sd, (void*)list->timestamp, TIME_LENGTH, 0);
                num_msg=htons(list->num_msg);
                send(sd,(void*)&num_msg, sizeof(uint16_t), 0);
            }
        }
    }
}

void send_hanging_msg(int sd){
    uint16_t len_username;
    char username[MAX_USERNAME];
    int i,j;
    struct PENDENTI* list;
    struct PENDENTI* prec;
    struct NOTIFICA* list_notifica;
    struct NOTIFICA* prec_notifica;
    uint16_t len_buffer;

    recv(sd, (void*)&len_username, sizeof(uint16_t), MSG_WAITALL);
    len_username=ntohs(len_username);
    recv(sd, (void*)username, len_username, MSG_WAITALL);

    send(sd, (void*)SHHNG, DIM_MSG_SERVER, 0);

    // devo controllare se lo username esiste nei pendenti dell'utente
    for(i=0; i<MAX_USERS; i++){
        if(USERS[i].sd==sd){
            printf("[USER %s HA RICHIESTO SHOW]\n", USERS[i].username);
            prec=USERS[i].chat_pendenti;
            for(list=USERS[i].chat_pendenti; list!=NULL; list=list->next){
                if(strcmp(list->username, username)==0){
                    // devo mandare lo username
                    len_username=htons(len_username);
                    send(sd,(void*)&len_username, sizeof(uint16_t), 0);
                    send(sd, (void*)username, strlen(username)+1, 0);
                    // devo mandare il buffer
                    len_buffer=htons(strlen(list->buffer)+1);
                    send(sd, (void*)&len_buffer, sizeof(uint16_t), 0);
                    send(sd, (void*)list->buffer, strlen(list->buffer)+1, 0);
                    // devo notificare all'altro username che i suoi messaggi sono stati letti
                    for(j=0; j<MAX_USERS; j++){
                        if(strcmp(USERS[j].username, list->username)==0){
                            // SE È ONLINE LO FACCIO SUBITO
                            if(strcmp(USERS[j].timestamp_logout, "\0")==0){
                                invio_notifica_pendenti(USERS[j].sd, USERS[i].username);
                                break;
                            }
                            else{
                                // significa che l'user non è online. Salvo nelle notifiche pendenti che verranno inviate non appena torna online
                                if(USERS[j].notifiche_pendenti==NULL){
                                    USERS[j].notifiche_pendenti=malloc(sizeof(struct NOTIFICA));
                                    strcpy(USERS[j].notifiche_pendenti->username, USERS[i].username);
                                    USERS[j].notifiche_pendenti->next=NULL;
                                    break;
                                }
                                prec_notifica=USERS[j].notifiche_pendenti;
                                for(list_notifica=USERS[j].notifiche_pendenti; list_notifica!=NULL; list_notifica=list_notifica->next){
                                    prec_notifica=list_notifica;
                                }
                                prec_notifica->next=malloc(sizeof(struct NOTIFICA));
                                strcpy(prec_notifica->next->username, USERS[i].username);
                                prec_notifica->next->next=NULL;
                                break;
                            }

                        }
                    }
                    // tolgo l'elemento dalla lista
                    if(list==USERS[i].chat_pendenti){
                        USERS[i].chat_pendenti=list->next;
                        return;
                    }
                    prec->next=list->next;
                    return;
                }
                prec=list;
            }
            // se arrivo qua significa che non ho trovato nessuno con quell'username tra i pendenti
            strcpy(username, "/NULL");
            len_username=strlen(username)+1;
            len_username=htons(len_username);
            send(sd,(void*)&len_username, sizeof(uint16_t), 0);
            send(sd, (void*)username, strlen(username)+1, 0);
        }
    }
}

void* receive_thread(void* arg){
    // sono nel processo figlio che si occupa delle receive
    fd_set read_fds;
    int i,len,ret,j;
    char buffer[DIM_MSG_SERVER];
    int new_sd;
    struct sockaddr_in client_addr;

    printf("[FIGLIO PER LA RICEZIONE ATTIVO]\n");
    while(1){
        read_fds=MASTER;
        select(FDMAX+1, &read_fds, NULL, NULL, NULL);
        for(i=0; i<=FDMAX; i++){
            if(FD_ISSET(i, &read_fds)){
                if(i==LISTENER){
                    len=sizeof(client_addr);
                    new_sd=accept(LISTENER, (struct sockaddr*)&client_addr, &len);

                    FD_SET(new_sd, &MASTER);
                    if(new_sd>FDMAX){
                        FDMAX=new_sd;
                    }
                }
                else{
                    ret=recv(i, (void*)buffer, sizeof(buffer), MSG_WAITALL);
                    if(ret==-1){
                        perror("ERRORE NELLA RECV: ");
                        exit(1);
                    }
                    if(ret==0){
                        for(j=0; j<MAX_USERS; j++){
                            if(USERS[j].sd==i){
                                printf("[USERNAME %s DISCONNESSO]\n", USERS[j].username);
                                // metto l'ora attuale come logout
                                modifica_login_logout(1, USERS[j].timestamp_login, USERS[j].timestamp_logout);
                                // tolgo l'utente dal master
                                FD_CLR(i, &MASTER);
                                USERS[j].flag_chat=0;
                                USERS[j].sd=-1;
                                close(i);
                                break;
                            }
                        }
                        break;
                    }

                    if(strcmp(buffer, REQUS)==0){
                        printf("[GESTISCO RICHIESTA IP/PORTA\n");
                        for(j=0; j<MAX_USERS; j++){
                            if(USERS[j].sd==i){
                                break;
                            }
                        }
                        richiesta_ip_porta(i, USERS[j].username);
                    }
                    if(strcmp(buffer, LOGIN)==0){
                        printf("[GESTISCO RICHIESTA LOGIN]\n");
                        richiesta_login(i);
                    }
                    if(strcmp(buffer, SIGIN)==0){
                        printf("[GESTISCO RICHIESTA SIGNIN]\n");
                        richiesta_sigin(i);
                    }
                    if(strcmp(buffer, CHCLS)==0){
                        // lo user ha chiuso la chat
                        for(j=0; j<MAX_USERS; j++){
                            if(USERS[j].sd==i){
                                printf("[USERNAME %s È USCITO DALLA CHAT]\n", USERS[j].username);
                                USERS[j].flag_chat=0;
                                break;
                            }
                        }
                    }
                    if(strcmp(buffer, CHSRV)==0){
                        // faccio lo storage del messaggio che mi è arrivato
                        for(j=0; j<MAX_USERS; j++){
                            if(USERS[j].sd==i){
                                USERS[j].flag_chat=1;
                                printf("[MESSAGGIO DI %s CONSERVATO]\n",USERS[j].username);
                                break;
                            }
                        }
                        store_msg(i);
                    }
                    if(strcmp(buffer, CHSRC)==0){
                        for(j=0; j<MAX_USERS; j++){
                            if(USERS[j].sd==i){
                                USERS[j].flag_chat=0;
                                printf("[%s USCITO DALLA CHAT COL SERVER]\n",USERS[j].username);
                                break;
                            }
                        }
                    }
                    if(strcmp(buffer, CHHNG)==0){
                        send_hanging(i);
                    }
                    if(strcmp(buffer, SHHNG)==0){
                        send_hanging_msg(i);
                    }
                }
            }
        }
    }
}

int main(){
    // variabili di utilità
    pid_t child_receive;
    char buffer[DIM_MSG_SERVER];
    
    // variabili per la select
    fd_set read_fds;

    // variabili del server
    struct sockaddr_in server_addr, client_addr;
    int new_sd;
    int dim, len, i,ret, optval=1;

    // inizializzazione variabili
    iniz_variabili();

    // creo il mio socket listener e vado nella select per gestire tutte le richieste
    FD_ZERO(&read_fds);
    LISTENER=socket(AF_INET, SOCK_STREAM, 0);
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(SRV_PORT);
    inet_pton(AF_INET, "localhost", &server_addr.sin_addr);

    setsockopt(LISTENER,SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    ret=bind(LISTENER, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(ret==-1){
        perror("Errore nella bind: ");
        exit(1);
    }
    listen(LISTENER, MAX_USERS);
    FD_SET(LISTENER, &MASTER);
    FDMAX=LISTENER;

    // CREAZIONE DEL THREAD DI RECEIVE
    ret=pthread_create(&REC_THREAD, NULL, receive_thread, NULL);
    if(ret){
        printf("ERRORE CREAZIONE THREAD");
        exit(-1);
    }
    show_menu();
}

